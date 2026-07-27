#pragma once
// =============================================================================
// ViewerInput.h — bắt phím/chuột trên CỬA SỔ VIDEO của màn Viewer, đẩy xuống
// phiên client qua C API. Tái sinh từ input/InputCapture cũ (bị gỡ ở M4b khi
// từ bản UI cũ) — logic hai chế độ chuột giữ nguyên:
//
//   TUYỆT ĐỐI (mặc định): WM_MOUSEMOVE -> toạ độ client chuẩn hoá 0..65535.
//   TƯƠNG ĐỐI (F9): delta thô từ Raw Input, con trỏ bị khoá (ClipCursor) và ẩn.
//       BẮT BUỘC cho game FPS — game đọc chuột thô rồi tự kéo con trỏ về giữa.
//
// BÀN PHÍM LUÔN QUA RAW INPUT: cần SCANCODE (game DirectInput đọc thẳng scancode,
// gửi mỗi vk thì game không thấy gì).
//
// Khác bản cũ đúng một chỗ: sink là DhClientHandle* + các hàm dh_client_* thay
// vì deskhub::InputEvent — UI chỉ nói chuyện qua C API.
//
// ⚠ Toàn bộ chạy trên LUỒNG MESSAGE. OnMessage phải NHẸ (dh_client_* chỉ đẩy
// hàng đợi, không gửi socket).
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

struct DhClientHandle;

class ViewerInput {
public:
    // Đăng ký Raw Input cho cửa sổ video. Gọi trên luồng bơm message.
    bool Attach(HWND hwnd, DhClientHandle* client);
    void Detach();

    // Gọi từ WndProc của cửa sổ video. true = đã tiêu thụ message.
    bool OnMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    // KHÔNG có công tắc bật/tắt: đã Attach là gửi input. Bản trước có SetEnabled cho
    // chế độ chỉ-xem — chế độ đó đã bỏ 2026-07-27 (app chỉ làm remote desktop).
    bool relativeMode() const {
        return relative_;
    }
    void ToggleRelativeMode(); // == F9 (nút Lock trên HUD gọi cùng đường)

private:
    void SetRelativeMode(bool on);
    void OnRawInput(LPARAM lp);
    void EmitButton(int button, bool down);

    HWND hwnd_ = nullptr;
    DhClientHandle* client_ = nullptr;
    bool relative_ = false;
    bool attached_ = false;
    int buttonsDown_ = 0; // đếm nút đang giữ -> biết khi nào nhả SetCapture
};
