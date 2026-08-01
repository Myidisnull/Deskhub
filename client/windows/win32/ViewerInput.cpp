#include "ViewerInput.h"

#include "deskhub/media/ViewFit.h"
#include "deskhub/protocol/Wire.h"

#include <windowsx.h>
#include <cstdio>

#include "deskhubp/diag/Log.h"
#include "deskhubp/ffi/ClientSession.h"

namespace {

constexpr USHORT kUsagePageGeneric = 0x01;
constexpr USHORT kUsageMouse = 0x02;
constexpr USHORT kUsageKeyboard = 0x06;
constexpr int kToggleRelativeKey = VK_F9;
using deskhub::kScanExtended;

int32_t Normalize(int v, uint32_t extent) {
    return deskhub::NormalizeAxis(double(v), double(extent));
}

deskhub::MouseButton XButtonOf(WPARAM wp) {
    return GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? deskhub::MouseButton::X1
                                              : deskhub::MouseButton::X2;
}

}

bool ViewerInput::Attach(HWND hwnd, DHSession* session) {
    if (!hwnd) return false;
    hwnd_ = hwnd;
    session_ = session;

    RAWINPUTDEVICE rid[2] = {};
    rid[0].usUsagePage = kUsagePageGeneric;
    rid[0].usUsage = kUsageMouse;
    rid[0].hwndTarget = hwnd;
    rid[1].usUsagePage = kUsagePageGeneric;
    rid[1].usUsage = kUsageKeyboard;
    rid[1].hwndTarget = hwnd;
    if (!RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE))) {
        LOGE("[Input] RegisterRawInputDevices failed: %lu", GetLastError());
        return false;
    }
    attached_ = true;
    return true;
}

void ViewerInput::Detach() {
    if (!attached_) return;
    ApplyLockEffect(pointer_.OnFocusLost());
    RAWINPUTDEVICE rid[2] = {};
    rid[0].usUsagePage = kUsagePageGeneric;
    rid[0].usUsage = kUsageMouse;
    rid[0].dwFlags = RIDEV_REMOVE;
    rid[1].usUsagePage = kUsagePageGeneric;
    rid[1].usUsage = kUsageKeyboard;
    rid[1].dwFlags = RIDEV_REMOVE;
    RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));
    attached_ = false;
    hwnd_ = nullptr;
    session_ = nullptr;
}

void ViewerInput::ToggleRelativeMode() {
    ApplyLockEffect(pointer_.OnToggleLockKey());
}

void ViewerInput::ApplyLockEffect(const deskhub::PointerLockEffect& effect) {
    if (effect.releaseHeldInput && session_) dh_session_release_all_input(session_);
    if (effect.lockChanged) GrabPointer(pointer_.locked());
}

void ViewerInput::GrabPointer(bool on) {
    if (on) {
        RECT r{};
        GetClientRect(hwnd_, &r);
        POINT tl{r.left, r.top}, br{r.right, r.bottom};
        ClientToScreen(hwnd_, &tl);
        ClientToScreen(hwnd_, &br);
        RECT screen{tl.x, tl.y, br.x, br.y};
        ClipCursor(&screen);
        while (ShowCursor(FALSE) >= 0) {}
        SetCapture(hwnd_);
    } else {
        ClipCursor(nullptr);
        while (ShowCursor(TRUE) < 0) {}
        if (GetCapture() == hwnd_ && buttonsDown_ == 0) ReleaseCapture();
    }
}

void ViewerInput::EmitButton(deskhub::MouseButton button, bool down) {
    if (down) {
        if (buttonsDown_++ == 0) SetCapture(hwnd_);
    } else if (buttonsDown_ > 0) {
        if (--buttonsDown_ == 0 && !pointer_.locked() && GetCapture() == hwnd_) ReleaseCapture();
    }
    if (session_) dh_session_mouse_button(session_, int32_t(button), down);
}

void ViewerInput::OnRawInput(LPARAM lp) {
    UINT size = 0;
    if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0)
        return;
    alignas(8) BYTE buf[sizeof(RAWINPUT) + 64];
    if (size > sizeof(buf)) return;
    if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) != size)
        return;
    const RAWINPUT* ri = (const RAWINPUT*)buf;

    if (ri->header.dwType == RIM_TYPEKEYBOARD) {
        const RAWKEYBOARD& kb = ri->data.keyboard;
        if (kb.VKey == 0xFF) return;
        const bool down = (kb.Flags & RI_KEY_BREAK) == 0;

        if (kb.VKey == kToggleRelativeKey) {
            if (down) ToggleRelativeMode();
            return;
        }

        int scan = kb.MakeCode;
        if (kb.Flags & RI_KEY_E0) scan |= kScanExtended;
        if (session_) dh_session_key(session_, int32_t(kb.VKey), scan, down);
        return;
    }

    if (ri->header.dwType == RIM_TYPEMOUSE && pointer_.locked()) {
        const RAWMOUSE& m = ri->data.mouse;
        if (m.usFlags & MOUSE_MOVE_ABSOLUTE) return;
        if ((m.lLastX || m.lLastY) && session_)
            dh_session_mouse_move_rel(session_, m.lLastX, m.lLastY);
    }
}

bool ViewerInput::OnMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (!attached_ || hwnd != hwnd_) return false;

    switch (msg) {
        case WM_INPUT:
            OnRawInput(lp);
            return false;

        case WM_MOUSEMOVE: {
            if (pointer_.locked()) return true;
            RECT r{};
            GetClientRect(hwnd_, &r);
            if (session_)
                dh_session_mouse_move(session_,
                    Normalize(GET_X_LPARAM(lp), uint32_t(r.right - r.left)),
                    Normalize(GET_Y_LPARAM(lp), uint32_t(r.bottom - r.top)));
            return true;
        }

        case WM_LBUTTONDOWN: EmitButton(deskhub::MouseButton::Left, true); return true;
        case WM_LBUTTONUP: EmitButton(deskhub::MouseButton::Left, false); return true;
        case WM_RBUTTONDOWN: EmitButton(deskhub::MouseButton::Right, true); return true;
        case WM_RBUTTONUP: EmitButton(deskhub::MouseButton::Right, false); return true;
        case WM_MBUTTONDOWN: EmitButton(deskhub::MouseButton::Middle, true); return true;
        case WM_MBUTTONUP: EmitButton(deskhub::MouseButton::Middle, false); return true;
        case WM_XBUTTONDOWN:
            EmitButton(XButtonOf(wp), true);
            return true;
        case WM_XBUTTONUP:
            EmitButton(XButtonOf(wp), false);
            return true;

        case WM_MOUSEWHEEL:
            if (session_) dh_session_mouse_wheel(session_, GET_WHEEL_DELTA_WPARAM(wp));
            return true;

        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_CHAR:
            return true;

        case WM_KILLFOCUS:
            ApplyLockEffect(pointer_.OnFocusLost());
            return false;
    }
    return false;
}
