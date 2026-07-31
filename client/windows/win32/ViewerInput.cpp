#include "ViewerInput.h"

#include "deskhub/media/ViewFit.h"

#include <windowsx.h>
#include <cstdio>

#include "DeskhubApi.h"

namespace {

constexpr USHORT kUsagePageGeneric = 0x01;
constexpr USHORT kUsageMouse = 0x02;
constexpr USHORT kUsageKeyboard = 0x06;
constexpr int kToggleRelativeKey = VK_F9;
constexpr int kScanExtended = 0x100;

int32_t Normalize(int v, uint32_t extent) {
    return deskhub::NormalizeAxis(double(v), double(extent));
}

}

bool ViewerInput::Attach(HWND hwnd, DhClientHandle* client) {
    if (!hwnd) return false;
    hwnd_ = hwnd;
    client_ = client;

    RAWINPUTDEVICE rid[2] = {};
    rid[0].usUsagePage = kUsagePageGeneric;
    rid[0].usUsage = kUsageMouse;
    rid[0].hwndTarget = hwnd;
    rid[1].usUsagePage = kUsagePageGeneric;
    rid[1].usUsage = kUsageKeyboard;
    rid[1].hwndTarget = hwnd;
    if (!RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE))) {
        std::printf("[Input] RegisterRawInputDevices failed: %lu\n", GetLastError());
        return false;
    }
    attached_ = true;
    return true;
}

void ViewerInput::Detach() {
    if (!attached_) return;
    SetRelativeMode(false);
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
    client_ = nullptr;
}

void ViewerInput::ToggleRelativeMode() {
    SetRelativeMode(!relative_);
}

void ViewerInput::SetRelativeMode(bool on) {
    if (relative_ == on) return;
    relative_ = on;
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

void ViewerInput::EmitButton(int button, bool down) {
    if (down) {
        if (buttonsDown_++ == 0) SetCapture(hwnd_);
    } else if (buttonsDown_ > 0) {
        if (--buttonsDown_ == 0 && !relative_ && GetCapture() == hwnd_) ReleaseCapture();
    }
    if (client_) dh_client_mouse_button(client_, button, down ? 1 : 0);
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
        if (client_) dh_client_key(client_, int(kb.VKey), scan, down ? 1 : 0);
        return;
    }

    if (ri->header.dwType == RIM_TYPEMOUSE && relative_) {
        const RAWMOUSE& m = ri->data.mouse;
        if (m.usFlags & MOUSE_MOVE_ABSOLUTE) return;
        if ((m.lLastX || m.lLastY) && client_)
            dh_client_mouse_move_rel(client_, m.lLastX, m.lLastY);
    }
}

bool ViewerInput::OnMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (!attached_ || hwnd != hwnd_) return false;

    switch (msg) {
        case WM_INPUT:
            OnRawInput(lp);
            return false;

        case WM_MOUSEMOVE: {
            if (relative_) return true;
            RECT r{};
            GetClientRect(hwnd_, &r);
            if (client_)
                dh_client_mouse_move(client_,
                    uint16_t(Normalize(GET_X_LPARAM(lp), uint32_t(r.right - r.left))),
                    uint16_t(Normalize(GET_Y_LPARAM(lp), uint32_t(r.bottom - r.top))));
            return true;
        }

        case WM_LBUTTONDOWN: EmitButton(0, true); return true;
        case WM_LBUTTONUP: EmitButton(0, false); return true;
        case WM_RBUTTONDOWN: EmitButton(1, true); return true;
        case WM_RBUTTONUP: EmitButton(1, false); return true;
        case WM_MBUTTONDOWN: EmitButton(2, true); return true;
        case WM_MBUTTONUP: EmitButton(2, false); return true;
        case WM_XBUTTONDOWN:
            EmitButton(GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? 3 : 4, true);
            return true;
        case WM_XBUTTONUP:
            EmitButton(GET_XBUTTON_WPARAM(wp) == XBUTTON1 ? 3 : 4, false);
            return true;

        case WM_MOUSEWHEEL:
            if (client_) dh_client_wheel(client_, GET_WHEEL_DELTA_WPARAM(wp));
            return true;

        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_CHAR:
            return true;

        case WM_KILLFOCUS:
            SetRelativeMode(false);
            return false;
    }
    return false;
}
