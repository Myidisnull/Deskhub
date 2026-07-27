// =============================================================================
// ViewerInput.cpp — cài đặt. Xem ViewerInput.h về hai chế độ chuột; các quyết
// định Raw Input giữ nguyên từ InputCapture cũ:
//   KHÔNG RIDEV_NOLEGACY — vẫn cần message thường (WM_MOUSEMOVE tuyệt đối, kéo
//     cửa sổ, WM_CLOSE). KHÔNG RIDEV_INPUTSINK — alt-tab ra ngoài thì gõ vào
//     máy mình như bình thường.
//   Khi bật gửi input, nuốt gần hết message phím (kể cả ESC) để người dùng gõ
//     vào MÁY KIA; riêng F9 là phím thoát hiểm, xử lý tại chỗ.
// =============================================================================
#include "ViewerInput.h"

#include <windowsx.h>
#include <cstdio>

#include "DeskhubApi.h"

namespace {

constexpr USHORT kUsagePageGeneric = 0x01;
constexpr USHORT kUsageMouse = 0x02;
constexpr USHORT kUsageKeyboard = 0x06;
constexpr int kToggleRelativeKey = VK_F9;
constexpr int kScanExtended = 0x100; // khớp deskhub::kScanExtended (Wire.h)

// Tọa độ client -> 0..65535. Mẫu số (n-1) để cạnh phải/dưới đạt đúng 65535.
int32_t Normalize(int v, uint32_t extent) {
    if (extent <= 1) return 0;
    if (v < 0) v = 0;
    if (uint32_t(v) >= extent) v = int(extent) - 1;
    return int32_t(int64_t(v) * 65535 / int64_t(extent - 1));
}

} // namespace

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

void ViewerInput::SetEnabled(bool on) {
    if (enabled_ == on) return;
    enabled_ = on;
    if (!on) SetRelativeMode(false);
}

void ViewerInput::ToggleRelativeMode() {
    if (enabled_) SetRelativeMode(!relative_);
}

// Ba việc phải làm cùng lúc khi khoá chuột: ClipCursor giữ con trỏ trong cửa sổ,
// ShowCursor ẩn nó (BỘ ĐẾM chứ không phải cờ — phải lặp tới khi dấu đổi),
// SetCapture để vẫn nhận message khi con trỏ chạm mép.
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

// Đếm nút đang giữ: nhả SetCapture sớm thì kéo-thả hai nút đứt giữa chừng và
// sự kiện nhả rơi ra ngoài cửa sổ -> kẹt nút ở máy host.
void ViewerInput::EmitButton(int button, bool down) {
    if (down) {
        if (buttonsDown_++ == 0) SetCapture(hwnd_);
    } else if (buttonsDown_ > 0) {
        if (--buttonsDown_ == 0 && !relative_ && GetCapture() == hwnd_) ReleaseCapture();
    }
    if (enabled_ && client_) dh_client_mouse_button(client_, button, down ? 1 : 0);
}

void ViewerInput::OnRawInput(LPARAM lp) {
    UINT size = 0;
    if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0)
        return;
    // Đệm stack (+64 dư cho cả hai loại thiết bị) — chuột sinh hàng trăm message
    // mỗi giây, không cấp phát trên đường nóng. alignas(8) vì RAWINPUT có trường 64-bit.
    alignas(8) BYTE buf[sizeof(RAWINPUT) + 64];
    if (size > sizeof(buf)) return;
    if (GetRawInputData((HRAWINPUT)lp, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) != size)
        return;
    const RAWINPUT* ri = (const RAWINPUT*)buf;

    if (ri->header.dwType == RIM_TYPEKEYBOARD) {
        const RAWKEYBOARD& kb = ri->data.keyboard;
        if (kb.VKey == 0xFF) return; // phím giả (vd. nửa Pause)
        const bool down = (kb.Flags & RI_KEY_BREAK) == 0;

        if (kb.VKey == kToggleRelativeKey) { // phím điều khiển cục bộ, không gửi
            if (down) ToggleRelativeMode();
            return;
        }

        int scan = kb.MakeCode;
        if (kb.Flags & RI_KEY_E0) scan |= kScanExtended;
        if (enabled_ && client_) dh_client_key(client_, int(kb.VKey), scan, down ? 1 : 0);
        return;
    }

    if (ri->header.dwType == RIM_TYPEMOUSE && relative_) {
        const RAWMOUSE& m = ri->data.mouse;
        // Chuột tuyệt đối (máy ảo/RDP/bảng vẽ) không cho delta -> bỏ, đường
        // tuyệt đối WM_MOUSEMOVE vẫn phục vụ các thiết bị đó.
        if (m.usFlags & MOUSE_MOVE_ABSOLUTE) return;
        if ((m.lLastX || m.lLastY) && enabled_ && client_)
            dh_client_mouse_move_rel(client_, m.lLastX, m.lLastY);
    }
}

bool ViewerInput::OnMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (!attached_ || hwnd != hwnd_) return false;

    switch (msg) {
        case WM_INPUT:
            OnRawInput(lp);
            return false; // WM_INPUT PHẢI tới DefWindowProc để hệ thống dọn

        case WM_MOUSEMOVE: {
            if (relative_) return true; // delta đã lấy từ Raw Input
            RECT r{};
            GetClientRect(hwnd_, &r);
            if (enabled_ && client_)
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
            if (enabled_ && client_) dh_client_wheel(client_, GET_WHEEL_DELTA_WPARAM(wp));
            return true;

        // Phím lấy qua WM_INPUT rồi; nuốt message thường để ESC trong game ở máy
        // kia không đóng gì ở máy này.
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_CHAR:
            return enabled_;

        case WM_KILLFOCUS:
            // Mất focus khi đang khoá -> thả, không thì người dùng kẹt con trỏ.
            SetRelativeMode(false);
            return false;
    }
    return false;
}
