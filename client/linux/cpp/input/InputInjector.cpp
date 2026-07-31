// =============================================================================
// InputInjector.cpp — dựng ba thiết bị ảo trên /dev/uinput rồi phát sự kiện.
//
// ⚠ BA CÁI BẪY CỦA uinput, GHI LẠI ĐỂ KHÔNG DẪM LẠI
//
//   1. PHẢI KHAI BÁO TRƯỚC, TẠO SAU. Mọi ioctl UI_SET_EVBIT/UI_SET_KEYBIT/
//      UI_SET_ABSBIT phải chạy TRƯỚC UI_DEV_CREATE. Khai một phím sau khi tạo
//      thiết bị thì sự kiện của phím đó bị kernel vứt im lặng.
//
//   2. UDEV CẦN VÀI CHỤC MILI GIÂY. Ngay sau UI_DEV_CREATE, thiết bị đã tồn tại
//      nhưng compositor chưa kịp mở nó qua libinput. Sự kiện phát trong khoảng đó
//      mất trắng. Nên Init ngủ một nhịp sau khi tạo xong CẢ BA thiết bị — đây là
//      lý do vài phím đầu tiên "không ăn" nếu bỏ dòng sleep đó.
//
//   3. SYN_REPORT SAU MỖI THAO TÁC LOGIC. Kernel gom các sự kiện giữa hai
//      SYN_REPORT thành một gói. Quên nó thì không gì xảy ra cả — và đây là lỗi
//      khó đoán nhất vì write() vẫn báo thành công.
//
// LIÊN QUAN: input/InputInjector.h (⚠ vì sao ba thiết bị, ánh xạ toạ độ),
//            input/LinuxKeyMap.h, input/LocalInputMonitor.h
// =============================================================================
#include "input/InputInjector.h"

#include <linux/uinput.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <thread>

#include "deskhubp/Log.h"
#include "deskhubp/Clock.h"
#include "input/LinuxKeyMap.h"
#include "input/LocalInputMonitor.h"

namespace {

// Thang của trục tuyệt đối. 65535 khớp đúng thang chuẩn hoá trên dây (Wire.h),
// nên phép đổi cuối cùng không mất độ chính xác nào.
constexpr int32_t kAbsMax = 65535;

// Bánh xe: giao thức đếm theo bội của 120 (WHEEL_DELTA của Windows), evdev đếm
// theo NẤC. Chia 120 để một nấc lăn ra đúng một nấc.
constexpr int32_t kWheelDelta = 120;

bool Emit(int fd, uint16_t type, uint16_t code, int32_t value) {
    if (fd < 0) return false;
    input_event ev{};
    ev.type = type;
    ev.code = code;
    ev.value = value;
    return write(fd, &ev, sizeof(ev)) == ssize_t(sizeof(ev));
}

// Bẫy số 3: đóng gói một thao tác logic.
bool Sync(int fd) {
    return Emit(fd, EV_SYN, SYN_REPORT, 0);
}

// Dựng một thiết bị uinput. `keys` là danh sách mã EV_KEY phải khai báo.
// `absAxes` khác nullptr thì thêm hai trục tuyệt đối X/Y.
int CreateDevice(const char* name, const uint16_t* keys, size_t keyCount, bool withRel,
    bool withAbs) {
    const int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        LOGE(
            "[Inject] Cannot open /dev/uinput (%s). Add a udev rule or join the 'input' group "
            "— see docs/17-linux-app.md §7.",
            std::strerror(errno));
        return -1;
    }

    // Bẫy số 1: khai báo hết năng lực TRƯỚC UI_DEV_CREATE.
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
    // VID/PID tuỳ ý nhưng phải ổn định: compositor nhớ cấu hình theo cặp này.
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

} // namespace

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
    // Desktop rỗng = máy một màn hình (hoặc UI không đo được): coi desktop CHÍNH
    // LÀ màn hình đang chia sẻ, phép đổi lồng nhau rút gọn thành phép đồng nhất.
    deskX_ = deskW ? deskX : srcX;
    deskY_ = deskH ? deskY : srcY;
    deskW_ = deskW ? deskW : srcW;
    deskH_ = deskH ? deskH : srcH;

    // Bàn phím: khai báo ĐÚNG những phím LinuxKeyMap biết dịch. Khai thừa cũng
    // được nhưng khai thiếu thì phím đó bị kernel vứt (bẫy số 1).
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

    // BTN_LEFT khai báo nhưng không bao giờ phát — xem ⚠ ở InputInjector.h.
    const uint16_t absButtons[] = {BTN_LEFT};
    absFd_ = CreateDevice(kAbsPointerName, absButtons, 1, false, true);

    if (kbdFd_ < 0 || mouseFd_ < 0 || absFd_ < 0) return false;

    // Bẫy số 2: cho udev + compositor kịp mở thiết bị.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    LOGI("[Inject] Virtual input ready — source %ux%u at %d,%d inside desktop %ux%u at %d,%d.",
        srcW_, srcH_, srcX_, srcY_, deskW_, deskH_, deskX_, deskY_);
    return true;
}

void InputInjector::Apply(const deskhub::InputEvent& e) {
    if (!enabled_ || kbdFd_ < 0) return;

    // "Host thắng": người ngồi tại máy vừa động vào chuột/phím thật thì input từ
    // xa nhường. Đếm riêng `skipped_` — đó là con số duy nhất phân biệt được "gõ
    // không ăn vì chủ máy đang dùng" với "không nhận được gói" (docs/09).
    if (localMon_ && localMon_->LocalActive(NowUs())) {
        ++skipped_;
        if (!localSuppressed_) {
            localSuppressed_ = true;
            LOGI("[Inject] Local user is typing — remote input yields.");
        }
        return;
    }
    if (localSuppressed_) localSuppressed_ = false;

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
    ++applied_;
}

void InputInjector::SendKey(int32_t vk, bool down) {
    uint16_t code = 0;
    // Ưu tiên sổ đang giữ khi NHẢ: host có thể gửi VK chung (0x10) lúc nhả trong
    // khi lúc nhấn là VK trái (0xA0). Tra sổ trước thì nhả đúng phím đã nhấn;
    // không thì phím kẹt vĩnh viễn và ReleaseAll cũng không cứu được vì sổ vẫn
    // còn mục đó.
    if (!down) {
        auto it = keysDown_.find(vk);
        if (it != keysDown_.end()) code = it->second;
    }
    if (!code && !linuxkeys::WinVkToEvdev(vk, code)) return;

    if (!Emit(kbdFd_, EV_KEY, code, down ? 1 : 0)) return;
    Sync(kbdFd_);

    if (down)
        keysDown_[vk] = code;
    else
        keysDown_.erase(vk);
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
    // Mọi cú nhấn đi qua thiết bị TƯƠNG ĐỐI, kể cả khi con trỏ vừa được đặt bằng
    // thiết bị tuyệt đối — xem ⚠ ở InputInjector.h.
    if (!Emit(mouseFd_, EV_KEY, code, down ? 1 : 0)) return;
    Sync(mouseFd_);

    if (down)
        buttonsDown_.insert(btn);
    else
        buttonsDown_.erase(btn);
}

// Hai phép đổi lồng nhau — xem "ánh xạ toạ độ" ở InputInjector.h.
void InputInjector::SendMoveAbsolute(int32_t nx, int32_t ny) {
    if (!srcW_ || !srcH_ || !deskW_ || !deskH_) return;
    const int32_t cx = nx < 0 ? 0 : (nx > kAbsMax ? kAbsMax : nx);
    const int32_t cy = ny < 0 ? 0 : (ny > kAbsMax ? kAbsMax : ny);

    // int64 suốt chặng: 65535 * 3840 đã tràn int32 nếu nhân trước khi chia.
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
    // Cuộn nhỏ hơn một nấc: làm tròn về ±1 thay vì nuốt mất. Bàn di của laptop
    // gửi rất nhiều bước nhỏ, nuốt hết thì cuộn bằng trackpad không hoạt động.
    const int32_t v = notches ? notches : (delta > 0 ? 1 : (delta < 0 ? -1 : 0));
    if (!v) return;
    Emit(mouseFd_, EV_REL, REL_WHEEL, v);
    Sync(mouseFd_);
}

// Duyệt trên BẢN SAO của sổ rồi xoá sổ: SendKey/SendButton tự sửa hai container
// này, không được vừa duyệt vừa sửa.
void InputInjector::ReleaseAll() {
    if (kbdFd_ < 0) return;
    const auto keys = keysDown_;
    for (const auto& [vk, code] : keys) {
        Emit(kbdFd_, EV_KEY, code, 0);
        Sync(kbdFd_);
    }
    keysDown_.clear();

    const auto buttons = buttonsDown_;
    for (deskhub::MouseButton b : buttons) SendButton(b, false);
    buttonsDown_.clear();
}
