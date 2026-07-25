#pragma once
// =============================================================================
// DeskhubApi.h — C API PHẲNG, ranh giới giữa lõi C++ và frontend C#/WinUI3.
//
// VÌ SAO CÓ FILE NÀY
//   App Windows nay chia hai lớp (giống client/android): lõi pipeline C++ ở
//   client/windows/cpp build thành deskhub_native.dll, còn giao diện ở
//   client/windows/csharp viết bằng WinUI3 (C#). C# không gọi thẳng lớp/hàm C++
//   được — nó P/Invoke qua một API C phẳng. File này LÀ hợp đồng đó.
//
// NGUYÊN TẮC GIỮ API DỄ P/INVOKE
//   • Chỉ dùng kiểu C: con trỏ, số nguyên cố định bề rộng, chuỗi char* UTF-8.
//   • KHÔNG trả std::string/std::vector/lớp qua ranh giới — layout khác nhau giữa
//     hai trình biên dịch, hủy nhầm heap là sập.
//   • Liệt kê bằng CALLBACK thay vì trả mảng: bên gọi khỏi phải đoán cỡ buffer,
//     và mọi chuỗi chỉ sống trong phạm vi callback (C# copy ngay sang string của nó).
//   • Mọi hàm là `extern "C"` (không name-mangling) + __declspec(dllexport).
//
// PHẠM VI M1
//   Mới có phần LIỆT KÊ (địa chỉ IP máy này, cửa sổ chia sẻ được) — đủ cho các màn
//   Home/Connect/SharePicker bên C#. Điều khiển phiên host/client (start/stop/stats)
//   và render video vào SwapChainPanel là M2/M3, sẽ thêm hàm vào chính file này.
//
// LIÊN QUAN: DeskhubApi.cpp (cài đặt), net/NetInfo.h, capture/WindowFinder.h,
//            client/windows/csharp/Interop/NativeMethods.cs (phía P/Invoke)
// =============================================================================
#include <stdint.h>

#if defined(_WIN32)
#define DH_API extern "C" __declspec(dllexport)
#define DH_CALL __stdcall
#else
#define DH_API extern "C"
#define DH_CALL
#endif

// Chuỗi truyền qua ranh giới đều là UTF-8, kết thúc bằng NUL. Chỉ hợp lệ TRONG lời
// gọi callback — bên nhận phải copy nếu muốn giữ.

// Callback cho mỗi địa chỉ IPv4 của máy: tên adapter ("Ethernet"/"Wi-Fi") + IP.
typedef void(DH_CALL* DhIpCallback)(const char* nameUtf8, const char* ipUtf8, void* user);

// Callback cho mỗi cửa sổ chia sẻ được. `hwnd` là HWND đóng gói thành uint64 để C#
// giữ lại và đưa xuống lúc bắt đầu share (M2). `minimized` = 1 nếu đang thu nhỏ.
typedef void(DH_CALL* DhWindowCallback)(uint64_t hwnd, const char* exeUtf8,
    const char* titleUtf8, uint32_t width, uint32_t height, int minimized, void* user);

// Liệt kê IPv4 của máy (chỉ adapter Up, bỏ loopback/APIPA). Gọi `cb` cho từng địa
// chỉ theo thứ tự hiển thị. Trả về số địa chỉ. cb có thể NULL để chỉ đếm.
DH_API int DH_CALL dh_list_local_ips(DhIpCallback cb, void* user);

// Liệt kê cửa sổ top-level chia sẻ được (đã lọc + sắp theo diện tích giảm dần). Gọi
// `cb` cho từng cửa sổ. Trả về số cửa sổ. cb có thể NULL để chỉ đếm.
DH_API int DH_CALL dh_list_windows(DhWindowCallback cb, void* user);

// Số phiên bản API, để C# kiểm tra DLL khớp lúc nạp. Tăng khi đổi/ thêm chữ ký hàm.
DH_API int DH_CALL dh_api_version(void);

// =============================================================================
// VAI HOST (M2) — chạy một phiên chia sẻ headless, do C# điều khiển.
//
// dh_agent_start phóng phiên trên MỘT thread nền (RunAgent chặn) và trả handle mờ.
// Mọi callback (rows/bound) chạy TRÊN THREAD ĐÓ — phía C# phải marshal về UI thread
// (DispatcherQueue). Chuỗi trong callback chỉ sống trong phạm vi lời gọi, C# copy ngay.
// =============================================================================
struct DhAgentHandle; // mờ; C# giữ như con trỏ (IntPtr)

// Một nguồn để chia sẻ: đúng MỘT trong hwnd/monitor khác 0 (đóng gói từ HWND/HMONITOR).
typedef struct DhAgentSource {
    uint64_t hwnd;
    uint64_t monitor;
    const char* name; // UTF-8, tên hiện ở danh sách phía client
} DhAgentSource;

typedef struct DhAgentOptions {
    uint16_t port;
    uint32_t fps;
    uint32_t bitrateMbps;
    int allowInput; // 1 = cho client điều khiển
} DhAgentOptions;

// Một dòng nguồn đang chia sẻ (ánh xạ từ SessionSourceRow) để C# hiện danh sách.
typedef struct DhAgentRow {
    uint8_t sourceId;
    const char* label; // UTF-8, ví dụ "Code — Deskhub  (1920x1080, viewer connected)"
    int pending;       // 1 = đang khởi động (chưa lên hình)
} DhAgentRow;

typedef void(DH_CALL* DhAgentRowsCallback)(const DhAgentRow* rows, int count, void* user);
typedef void(DH_CALL* DhAgentBoundCallback)(uint16_t port, void* user);

// Bắt đầu phiên host phục vụ `sources` (count ≥ 1). Trả handle, hoặc NULL nếu tham số
// sai. Giải phóng bằng dh_agent_stop.
DH_API DhAgentHandle* DH_CALL dh_agent_start(const DhAgentSource* sources, int count,
    const DhAgentOptions* opt, DhAgentRowsCallback rowsCb, DhAgentBoundCallback boundCb,
    void* user);

// Thêm một cửa sổ vào phiên đang chạy (nút "Add source").
DH_API void DH_CALL dh_agent_add_window(DhAgentHandle* h, uint64_t hwnd, const char* name);

// Tắt một nguồn theo sourceId (nút "Stop" của một dòng).
DH_API void DH_CALL dh_agent_remove(DhAgentHandle* h, uint8_t sourceId);

// Dừng phiên, join thread, giải phóng handle. `h` không dùng được sau lời gọi này.
DH_API void DH_CALL dh_agent_stop(DhAgentHandle* h);

// =============================================================================
// VAI CLIENT / VIEWER (M3) — xem một nguồn, render vào SwapChainPanel của C#.
//
// LUỒNG GẮN SWAPCHAIN
//   dh_client_start tạo D3D11 device + composition swapchain (cỡ tạm) NGAY trên thread
//   gọi (UI của C#) rồi phóng recv/decode trên thread nền. C# lấy con trỏ swapchain qua
//   dh_client_swapchain và gắn vào SwapChainPanel (ISwapChainPanelNative::SetSwapChain).
//   Kích thước thật báo qua sizeCb để C# đặt tỷ lệ khung; video tự ResizeBuffers.
//
// INPUT ĐI TỪ C#: SwapChainPanel bắt chuột/phím rồi gọi các hàm dh_client_* dưới đây;
// native dịch sang InputEvent và gửi cho host. Toạ độ chuột CHUẨN HOÁ 0..65535 trong
// khung video (giống InputCapture cũ) để host thu nhỏ/phóng to vẫn trỏ đúng.
// =============================================================================
struct DhClientHandle; // mờ

// Dòng số liệu để hiện ở thanh trên (fps/Mbps/loss/RTT/e2e). Chạy trên thread nền.
typedef void(DH_CALL* DhClientStatsCallback)(const char* statsUtf8, void* user);
// Kích thước video đàm phán được (để C# đặt tỷ lệ SwapChainPanel). Chạy trên thread nền.
typedef void(DH_CALL* DhClientSizeCallback)(uint32_t width, uint32_t height, void* user);
// Phiên kết thúc (BYE/timeout/lỗi). Chạy trên thread nền.
typedef void(DH_CALL* DhClientClosedCallback)(const char* reasonUtf8, void* user);

// Bắt đầu xem `addr` ("ip[:port]", port mặc định 47777) nguồn `sourceId`. `sendInput`=1
// cho phép gửi chuột/phím. Trả handle hoặc NULL nếu lỗi. Giải phóng bằng dh_client_stop.
DH_API DhClientHandle* DH_CALL dh_client_start(const char* addrUtf8, uint8_t sourceId,
    int sendInput, DhClientStatsCallback statsCb, DhClientSizeCallback sizeCb,
    DhClientClosedCallback closedCb, void* user);

// Con trỏ IDXGISwapChain1 để C# gắn vào SwapChainPanel. NULL nếu chưa sẵn sàng.
DH_API void* DH_CALL dh_client_swapchain(DhClientHandle* h);

// --- Input (chỉ có tác dụng khi sendInput=1) ---
// Chuột di chuyển: toạ độ chuẩn hoá 0..65535 trong khung video.
DH_API void DH_CALL dh_client_mouse_move(DhClientHandle* h, uint16_t nx, uint16_t ny);
// Nút chuột: button 0=trái,1=phải,2=giữa; down=1 nhấn, 0 nhả.
DH_API void DH_CALL dh_client_mouse_button(DhClientHandle* h, int button, int down);
// Lăn chuột: delta bội số của 120 (một nấc = 120).
DH_API void DH_CALL dh_client_wheel(DhClientHandle* h, int delta);
// Phím: vk = mã phím ảo, scan = scancode (cộng 0x100 nếu phím mở rộng E0), down=1/0.
DH_API void DH_CALL dh_client_key(DhClientHandle* h, int vk, int scan, int down);

// Dừng phiên, join thread, giải phóng handle. `h` không dùng được sau lời gọi này.
DH_API void DH_CALL dh_client_stop(DhClientHandle* h);
