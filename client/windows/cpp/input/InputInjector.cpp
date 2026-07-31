#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include "input/InputInjector.h"

#include <cstdio>

#include "input/LocalInputMonitor.h"
#include "deskhubp/Clock.h"

#pragma comment(lib, "user32.lib")

namespace {

constexpr uint64_t kHostWinsGraceUs = 1'000'000;

void ScreenToVirtualDesk(int px, int py, LONG& nx, LONG& ny) {
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    nx = vw > 1 ? LONG(int64_t(px - vx) * 65535 / (vw - 1)) : 0;
    ny = vh > 1 ? LONG(int64_t(py - vy) * 65535 / (vh - 1)) : 0;
}

DWORD ButtonFlag(deskhub::MouseButton b, bool down) {
    switch (b) {
        case deskhub::MouseButton::Left: return down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        case deskhub::MouseButton::Right: return down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
        case deskhub::MouseButton::Middle: return down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
        case deskhub::MouseButton::X1:
        case deskhub::MouseButton::X2: return down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
    }
    return 0;
}

}

bool InputInjector::Init(HMONITOR monitor) {
    if (!monitor) return false;
    monitor_ = monitor;
    return true;
}

void InputInjector::SetEnabled(bool on) {
    if (enabled_ == on) return;
    if (!on) ReleaseAll();
    enabled_ = on;
}

void InputInjector::SendKey(int32_t vk, int32_t scan, bool down) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    if (!(scan & 0xFF)) scan = int32_t(MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC));
    if (scan & 0xFF) {
        in.ki.wScan = WORD(scan & 0xFF);
        in.ki.dwFlags |= KEYEVENTF_SCANCODE;
        if (scan & deskhub::kScanExtended) in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    } else {
        in.ki.wVk = WORD(vk);
    }
    SendInput(1, &in, sizeof(INPUT));
}

void InputInjector::SendButton(deskhub::MouseButton btn, bool down) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = ButtonFlag(btn, down);
    if (btn == deskhub::MouseButton::X1) in.mi.mouseData = XBUTTON1;
    if (btn == deskhub::MouseButton::X2) in.mi.mouseData = XBUTTON2;
    if (in.mi.dwFlags) SendInput(1, &in, sizeof(INPUT));
}

void InputInjector::SendMoveAbsolute(int32_t nx, int32_t ny) {
    MONITORINFO mi{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(monitor_, &mi)) return;
    const int w = mi.rcMonitor.right - mi.rcMonitor.left;
    const int h = mi.rcMonitor.bottom - mi.rcMonitor.top;
    if (w <= 1 || h <= 1) return;
    POINT pt{};
    pt.x = mi.rcMonitor.left + LONG(int64_t(nx) * (w - 1) / 65535);
    pt.y = mi.rcMonitor.top + LONG(int64_t(ny) * (h - 1) / 65535);

    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    ScreenToVirtualDesk(pt.x, pt.y, in.mi.dx, in.mi.dy);
    SendInput(1, &in, sizeof(INPUT));
}

void InputInjector::SendMoveRelative(int32_t dx, int32_t dy) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    in.mi.dx = dx;
    in.mi.dy = dy;
    SendInput(1, &in, sizeof(INPUT));
}

void InputInjector::Apply(const deskhub::InputEvent& e) {
    if (!enabled_ || !monitor_) return;

    const uint64_t lastLocal = LocalInputMonitor::LastPhysicalUs();
    if (lastLocal && NowUs() - lastLocal < kHostWinsGraceUs) {
        if (!localSuppressed_) {
            localSuppressed_ = true;
            std::printf(
                "[Inject] Local user is active - pausing remote input (host wins).\n");
            ReleaseAll();
        }
        ++skipped_;
        return;
    }
    if (localSuppressed_) {
        localSuppressed_ = false;
        std::printf("[Inject] Local user idle - remote input resumed.\n");
    }
    ++applied_;

    switch (e.type) {
        case deskhub::InputType::Key: {
            const bool down = e.state != 0;
            if (down)
                keysDown_[e.b] = e.a;
            else
                keysDown_.erase(e.b);
            SendKey(e.a, e.b, down);
            break;
        }
        case deskhub::InputType::MouseMove:
            if (e.absolute)
                SendMoveAbsolute(e.a, e.b);
            else
                SendMoveRelative(e.a, e.b);
            break;
        case deskhub::InputType::MouseButton: {
            const auto btn = deskhub::MouseButton(e.a);
            const bool down = e.state != 0;
            if (down)
                buttonsDown_.insert(btn);
            else
                buttonsDown_.erase(btn);
            SendButton(btn, down);
            break;
        }
        case deskhub::InputType::MouseWheel: {
            INPUT in{};
            in.type = INPUT_MOUSE;
            in.mi.dwFlags = MOUSEEVENTF_WHEEL;
            in.mi.mouseData = DWORD(e.b);
            SendInput(1, &in, sizeof(INPUT));
            break;
        }
    }
}

void InputInjector::ReleaseAll() {
    if (keysDown_.empty() && buttonsDown_.empty()) return;
    std::printf("[Inject] Releasing %zu keys + %zu mouse buttons still held.\n",
        keysDown_.size(), buttonsDown_.size());
    for (const auto& [scan, vk] : keysDown_) SendKey(vk, scan, false);
    for (auto btn : buttonsDown_) SendButton(btn, false);
    keysDown_.clear();
    buttonsDown_.clear();
}
