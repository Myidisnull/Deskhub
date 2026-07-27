#pragma once
// =============================================================================
// ScreenCapture.h — bắt hình MỘT MÀN HÌNH bằng Windows Graphics Capture (WGC).
// Đầu nguồn của toàn bộ luồng video. (Đường bắt theo cửa sổ — HWND — đã bỏ
// 2026-07-27 cùng tính năng share từng cửa sổ; nguồn chỉ còn là màn hình.)
//
// NHIỆM VỤ
//   Biến một HMONITOR thành dòng texture D3D11 chảy đều đặn ra callback.
//
// VỊ TRÍ TRONG LUỒNG DỮ LIỆU
//   DisplayFinder → **ScreenCapture** → IVideoEncoder → Packetizer → Pacer → UDP
//
// BA QUYẾT ĐỊNH THIẾT KẾ
//   1. THEO SỰ KIỆN, KHÔNG POLLING. WGC bắn FrameArrived mỗi khi nội dung đổi. Hỏi
//      vòng theo nhịp cố định vừa thêm độ trễ (trung bình nửa chu kỳ) vừa đốt CPU
//      khi màn hình đứng yên. Đổi lại: callback chạy trên thread của WGC, không
//      phải thread ta kiểm soát — xem cảnh báo bên dưới.
//
//   2. PIMPL ĐỂ GIẤU winrt. Toàn bộ C++/WinRT nằm trong .cpp. Header này chỉ lộ
//      D3D11/COM thuần, nên encoder tiêu thụ được mà không phải kéo theo bộ header
//      WinRT rất nặng (xem thêm CaptureTypes.h).
//
//   3. CHIA SẺ D3D11 DEVICE ra ngoài qua Device()/Context(). Encoder dùng chung
//      device thì texture không bao giờ phải rời VRAM — xem GpuSelect.h.
//
// ⚠ CALLBACK CHẠY TRÊN THREAD-POOL CỦA WGC
//   Hai hệ quả bắt buộc phải nhớ:
//     - Phải xử lý NHANH. Giữ chỗ trong frame pool lâu là làm nghẽn cả đường bắt
//       hình. Tuyệt đối không ngủ trong đó (xem Pacer.h — bài học phải trả giá).
//     - Không giữ FrameInfo::texture sau khi callback trả về (xem CaptureTypes.h).
//
// LIÊN QUAN: capture/CaptureTypes.h (FrameInfo), capture/GpuSelect.h (device dùng
//            chung), capture/DisplayFinder.h (nguồn HMONITOR), AgentLoop.cpp
// =============================================================================
#include "capture/CaptureTypes.h"
#include <functional>
#include <memory>

namespace capture {

// Khởi tạo runtime WinRT (MTA) cho luồng hiện tại. Gọi một lần lúc khởi động,
// trước khi tạo ScreenCapture.
void InitRuntime();

} // namespace capture

class ScreenCapture {
public:
    // Callback được gọi trên luồng của thread-pool WGC mỗi khi có frame mới.
    // Phải xử lý nhanh (encode/copy) và KHÔNG giữ FrameInfo::texture sau khi trả về.
    using FrameHandler = std::function<void(const FrameInfo&)>;

    ScreenCapture();
    ~ScreenCapture();
    ScreenCapture(const ScreenCapture&) = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;

    // Bắt đầu bắt hình màn hình `monitor`, gọi `onFrame` cho mỗi frame.
    // `device`: D3D11 device dùng chung (từ GpuSelect). Nếu nullptr, tự tạo device mặc định.
    // Dùng chung device với encoder để texture không phải copy chéo GPU.
    bool Start(HMONITOR monitor, ID3D11Device* device, FrameHandler onFrame);
    void Stop();

    // True khi màn hình mục tiêu đã bị ngắt (rút cáp / đổi cấu hình multi-monitor).
    bool Closed() const;

    // D3D11 device/context dùng cho capture - chia sẻ cho encoder (COM thuần).
    ID3D11Device* Device() const;
    ID3D11DeviceContext* Context() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
