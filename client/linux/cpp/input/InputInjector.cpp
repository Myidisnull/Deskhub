#include "input/InputInjector.h"

#include <linux/uinput.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>

#include "deskhubp/diag/Log.h"
#include "deskhubp/system/Clock.h"
#include "input/LinuxKeyMap.h"
#include "deskhubp/input/LocalInput.h"

namespace {

constexpr int32_t kAbsMax = 65535;

constexpr int32_t kWheelDelta = 120;

bool Emit(int fd, uint16_t type, uint16_t code, int32_t value) {
    if (fd < 0) return false;
    input_event ev{};
    ev.type = type;
    ev.code = code;
    ev.value = value;
    return write(fd, &ev, sizeof(ev)) == ssize_t(sizeof(ev));
}

bool Sync(int fd) {
    return Emit(fd, EV_SYN, SYN_REPORT, 0);
}

int CreateDevice(const char* name, const uint16_t* keys, size_t keyCount, bool withRel,
    bool withAbs) {
    const int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        LOGE(
            "[Inject] Cannot open /dev/uinput (%s). Run 'make setup-linux-permissions' to add "
            "the udev rule and join the 'input' group.",
            std::strerror(errno));
        return -1;
    }

    if (keyCount) {
        ioctl(fd, UI_SET_EVBIT, EV_KEY);
        for (size_t i = 0; i < keyCount; ++i) ioctl(fd, UI_SET_KEYBIT, keys[i]);
    }
    if (withRel) {
        ioctl(fd, UI_SET_EVBIT, EV_REL);
        ioctl(fd, UI_SET_RELBIT, REL_X);
        ioctl(fd, UI_SET_RELBIT, REL_Y);
        ioctl(fd, UI_SET_RELBIT, REL_WHEEL);
        ioctl(fd, UI_SET_RELBIT, REL_HWHEEL);
    }
    if (withAbs) {
        ioctl(fd, UI_SET_EVBIT, EV_ABS);
        ioctl(fd, UI_SET_ABSBIT, ABS_X);
        ioctl(fd, UI_SET_ABSBIT, ABS_Y);
        for (uint16_t axis : {uint16_t(ABS_X), uint16_t(ABS_Y)}) {
            uinput_abs_setup abs{};
            abs.code = axis;
            abs.absinfo.minimum = 0;
            abs.absinfo.maximum = kAbsMax;
            ioctl(fd, UI_ABS_SETUP, &abs);
        }
    }

    uinput_setup us{};
    us.id.bustype = BUS_VIRTUAL;
    us.id.vendor = 0xDE5C;
    us.id.product = 0x4855;
    us.id.version = 1;
    std::snprintf(us.name, sizeof(us.name), "%s", name);
    if (ioctl(fd, UI_DEV_SETUP, &us) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        LOGE("[Inject] Failed to create virtual device \"%s\": %s", name, std::strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

}

InputInjector::InputInjector() = default;

InputInjector::~InputInjector() {
    ReleaseAll();
    for (int* fd : {&kbdFd_, &mouseFd_, &absFd_}) {
        if (*fd >= 0) {
            ioctl(*fd, UI_DEV_DESTROY);
            close(*fd);
            *fd = -1;
        }
    }
}

bool InputInjector::Init(int32_t srcX, int32_t srcY, uint32_t srcW, uint32_t srcH, int32_t deskX,
    int32_t deskY, uint32_t deskW, uint32_t deskH) {
    srcX_ = srcX;
    srcY_ = srcY;
    srcW_ = srcW;
    srcH_ = srcH;
    deskX_ = deskW ? deskX : srcX;
    deskY_ = deskH ? deskY : srcY;
    deskW_ = deskW ? deskW : srcW;
    deskH_ = deskH ? deskH : srcH;

    uint16_t keys[256];
    size_t nKeys = 0;
    for (int vk = 0; vk < 256 && nKeys < 256; ++vk) {
        uint16_t code = 0;
        if (linuxkeys::WinVkToEvdev(vk, code)) keys[nKeys++] = code;
    }
    kbdFd_ = CreateDevice(kKeyboardName, keys, nKeys, false, false);

    const uint16_t buttons[] = {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA};
    mouseFd_ = CreateDevice(kPointerName, buttons, sizeof(buttons) / sizeof(buttons[0]), true,
        false);

    const uint16_t absButtons[] = {BTN_LEFT};
    absFd_ = CreateDevice(kAbsPointerName, absButtons, 1, false, true);

    if (kbdFd_ < 0 || mouseFd_ < 0 || absFd_ < 0) return false;

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    LOGI("[Inject] Virtual input ready — source %ux%u at %d,%d inside desktop %ux%u at %d,%d.",
        srcW_, srcH_, srcX_, srcY_, deskW_, deskH_, deskX_, deskY_);
    return true;
}

void InputInjector::Apply(const deskhub::InputEvent& e) {
    if (!enabled_ || kbdFd_ < 0) return;

    const deskhub::InputGate gate = held_.Gate(localMon_ && localMon_->LocalActive(NowUs()));
    if (!gate.allow) {
        if (gate.justSuppressed) LOGI("[Inject] Local user is typing — remote input yields.");
        return;
    }

    switch (e.type) {
        case deskhub::InputType::Key: SendKey(e.a, e.state != 0); break;
        case deskhub::InputType::MouseButton:
            SendButton(deskhub::MouseButton(e.a), e.state != 0);
            break;
        case deskhub::InputType::MouseMove:
            if (e.absolute)
                SendMoveAbsolute(e.a, e.b);
            else
                SendMoveRelative(e.a, e.b);
            break;
        case deskhub::InputType::MouseWheel: SendWheel(e.b); break;
        default: return;
    }
    held_.CountApplied();
}

void InputInjector::SendKey(int32_t vk, bool down) {
    uint16_t code = 0;
    if (!down) {
        if (const uint16_t* stored = held_.FindKey(vk)) code = *stored;
    }
    if (!code && !linuxkeys::WinVkToEvdev(vk, code)) return;

    if (!Emit(kbdFd_, EV_KEY, code, down ? 1 : 0)) return;
    Sync(kbdFd_);

    held_.SetKey(vk, code, down);
}

void InputInjector::SendButton(deskhub::MouseButton btn, bool down) {
    uint16_t code = 0;
    switch (btn) {
        case deskhub::MouseButton::Left: code = BTN_LEFT; break;
        case deskhub::MouseButton::Right: code = BTN_RIGHT; break;
        case deskhub::MouseButton::Middle: code = BTN_MIDDLE; break;
        case deskhub::MouseButton::X1: code = BTN_SIDE; break;
        case deskhub::MouseButton::X2: code = BTN_EXTRA; break;
        default: return;
    }
    if (!Emit(mouseFd_, EV_KEY, code, down ? 1 : 0)) return;
    Sync(mouseFd_);

    held_.SetButton(btn, down);
}

void InputInjector::SendMoveAbsolute(int32_t nx, int32_t ny) {
    if (!srcW_ || !srcH_ || !deskW_ || !deskH_) return;
    const int32_t cx = nx < 0 ? 0 : (nx > kAbsMax ? kAbsMax : nx);
    const int32_t cy = ny < 0 ? 0 : (ny > kAbsMax ? kAbsMax : ny);

    const int64_t globalX = srcX_ + int64_t(cx) * srcW_ / kAbsMax;
    const int64_t globalY = srcY_ + int64_t(cy) * srcH_ / kAbsMax;
    int64_t ax = (globalX - deskX_) * kAbsMax / deskW_;
    int64_t ay = (globalY - deskY_) * kAbsMax / deskH_;
    ax = ax < 0 ? 0 : (ax > kAbsMax ? kAbsMax : ax);
    ay = ay < 0 ? 0 : (ay > kAbsMax ? kAbsMax : ay);

    Emit(absFd_, EV_ABS, ABS_X, int32_t(ax));
    Emit(absFd_, EV_ABS, ABS_Y, int32_t(ay));
    Sync(absFd_);
}

void InputInjector::SendMoveRelative(int32_t dx, int32_t dy) {
    if (!dx && !dy) return;
    if (dx) Emit(mouseFd_, EV_REL, REL_X, dx);
    if (dy) Emit(mouseFd_, EV_REL, REL_Y, dy);
    Sync(mouseFd_);
}

void InputInjector::SendWheel(int32_t delta) {
    const int32_t notches = delta / kWheelDelta;
    const int32_t v = notches ? notches : (delta > 0 ? 1 : (delta < 0 ? -1 : 0));
    if (!v) return;
    Emit(mouseFd_, EV_REL, REL_WHEEL, v);
    Sync(mouseFd_);
}

void InputInjector::ReleaseAll() {
    if (kbdFd_ < 0) return;
    for (const auto& key : held_.TakeHeldKeys()) {
        Emit(kbdFd_, EV_KEY, key.native, 0);
        Sync(kbdFd_);
    }
    for (deskhub::MouseButton b : held_.TakeHeldButtons()) SendButton(b, false);
}
