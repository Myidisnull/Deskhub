#pragma once
// =============================================================================
// DeskhubApi.h — C API của VAI CLIENT trên Windows (dh_client_*).
//
// VÌ SAO CÒN FILE NÀY
//   Vai client chạy headless trong ClientApi.cpp (recv/decode/render một nguồn);
//   app Win32 (client/windows/win32) compile thẳng file đó vào exe và điều khiển
//   nó qua các hàm dưới đây — Viewer.cpp mở phiên, ViewerInput.cpp bơm chuột/phím.
//   API giữ dạng C phẳng: hợp đồng hẹp, không lộ lớp C++, và mọi chuỗi là UTF-8
//   kết thúc NUL chỉ sống trong callback.
//
// MÔ HÌNH LUỒNG
//   dh_client_start_hwnd tạo device + swapchain NGAY trên thread gọi (UI) rồi chạy
//   recv/decode trên thread nền. Mọi callback (stats/size/closed) chạy trên thread
//   nền đó — bên nhận tự marshal về UI thread nếu cần đụng cửa sổ.
//
// LIÊN QUAN: ClientApi.cpp (cài đặt), client/windows/win32/Viewer.h (người dùng),
//            net/SourceQuery.h (hỏi SOURCE_LIST — gọi thẳng C++, không qua API này)
// =============================================================================
#include <stdint.h>

#if defined(_WIN32)
#define DH_API extern "C" __declspec(dllexport)
#define DH_CALL __stdcall
#else
#define DH_API extern "C"
#define DH_CALL
#endif

struct DhClientHandle; // mờ

// Dòng số liệu để hiện ở thanh trên (fps/Mbps/loss/RTT/e2e). Chạy trên thread nền.
typedef void(DH_CALL* DhClientStatsCallback)(const char* statsUtf8, void* user);
// Kích thước video đàm phán được (để đặt tỷ lệ khung cửa sổ). Chạy trên thread nền.
typedef void(DH_CALL* DhClientSizeCallback)(uint32_t width, uint32_t height, void* user);
// Phiên kết thúc (BYE/timeout/lỗi). Chạy trên thread nền.
typedef void(DH_CALL* DhClientClosedCallback)(const char* reasonUtf8, void* user);

// Bắt đầu xem `addrUtf8` (CHỈ địa chỉ IP — cổng cố định 47777, xem net/UdpSocket.h)
// nguồn `sourceId`, render vào `hwnd` (cửa sổ CON do app cấp, đóng gói HWND thành
// uint64) qua swapchain-for-HWND. Backbuffer = cỡ video, DXGI stretch ra cỡ cửa sổ —
// app giữ tỷ lệ bằng cách đặt cỡ cửa sổ con theo khung video (sizeCb báo cỡ).
// Chuột/bàn phím luôn được gửi; không còn tham số bật/tắt.
// Trả handle hoặc NULL nếu tham số sai/thiếu GPU. Giải phóng bằng dh_client_stop.
DH_API DhClientHandle* DH_CALL dh_client_start_hwnd(const char* addrUtf8, uint8_t sourceId,
    uint64_t hwnd, DhClientStatsCallback statsCb, DhClientSizeCallback sizeCb,
    DhClientClosedCallback closedCb, void* user);

// --- Input ---
// Chuột di chuyển: toạ độ chuẩn hoá 0..65535 trong khung video.
DH_API void DH_CALL dh_client_mouse_move(DhClientHandle* h, uint16_t nx, uint16_t ny);
// Chuột di chuyển TƯƠNG ĐỐI (chế độ khoá chuột F9): delta thô theo pixel. Host bơm
// bằng MOUSEEVENTF_MOVE nên game đọc được — khác đường tuyệt đối ở trên.
DH_API void DH_CALL dh_client_mouse_move_rel(DhClientHandle* h, int dx, int dy);
// Nút chuột: button 0=trái,1=phải,2=giữa,3=X1,4=X2; down=1 nhấn, 0 nhả.
DH_API void DH_CALL dh_client_mouse_button(DhClientHandle* h, int button, int down);
// Lăn chuột: delta bội số của 120 (một nấc = 120).
DH_API void DH_CALL dh_client_wheel(DhClientHandle* h, int delta);
// Phím: vk = mã phím ảo, scan = scancode (cộng 0x100 nếu phím mở rộng E0), down=1/0.
DH_API void DH_CALL dh_client_key(DhClientHandle* h, int vk, int scan, int down);

// Dừng phiên, join thread, giải phóng handle. `h` không dùng được sau lời gọi này.
DH_API void DH_CALL dh_client_stop(DhClientHandle* h);
