#pragma once
// =============================================================================
// ScreenCapture.h — bắt hình MỘT MÀN HÌNH qua PipeWire. Đầu nguồn của toàn bộ
//                   luồng video. Đối ứng client/windows/cpp/capture/ScreenCapture.h
//                   (WGC) và client/macos/.../capture/ScreenCapture.h
//                   (ScreenCaptureKit).
//
// NHIỆM VỤ
//   Biến một node PipeWire (do PortalScreenCast xin được) thành dòng frame chảy
//   đều đặn ra callback.
//
// VỊ TRÍ TRONG LUỒNG DỮ LIỆU
//   PortalScreenCast → **ScreenCapture** → VaEncoder → Packetizer → UDP
//
// BỐN QUYẾT ĐỊNH THIẾT KẾ
//   1. THEO SỰ KIỆN, KHÔNG POLLING. PipeWire đẩy buffer ra callback `process`;
//      compositor chỉ phát frame khi nội dung ĐỔI. Đây chính là hành vi "WGC chỉ
//      phát frame khi nội dung đổi" bên Windows và "SCFrameStatusIdle" bên macOS —
//      nên cơ chế CACHE FRAME CUỐI ở AgentLoop vẫn cần y nguyên, vì cùng một lý do:
//      nguồn đứng im thì không có gì để nén khi client xin IDR.
//
//   2. PIMPL ĐỂ GIẤU PIPEWIRE. Toàn bộ pipewire/*.h và spa/*.h nằm trong .cpp.
//      Header này chỉ lộ C++ thuần, nên AgentLoop.cpp không phải kéo theo bộ header
//      khổng lồ của SPA (xem CaptureTypes.h).
//
//   3. MỘT THREAD LOOP CHO MỖI NGUỒN. pw_thread_loop tự dựng thread riêng và
//      callback chạy trên đó — đối ứng chính xác của "queue riêng mỗi SCStream" bên
//      macOS. N màn hình = N thread capture, đúng mô hình luồng mà AgentLoop.cpp mô
//      tả ở đầu file.
//
//   4. THOẢ THUẬN CẢ HAI ĐƯỜNG BỘ NHỚ. Ta chào PipeWire HAI định dạng: một có
//      thuộc tính modifier (mở đường dma-buf, zero-copy) và một không (lối lùi
//      MemFd/MemPtr). PipeWire chọn cái nào chạy được; callback biết mình đang ở
//      đường nào qua LinuxFrameInfo::memory. Chi tiết vì sao ở CaptureTypes.h.
//
// ⚠ CALLBACK CHẠY TRÊN THREAD CỦA PIPEWIRE
//   Hai hệ quả bắt buộc phải nhớ:
//     - Phải xử lý NHANH. Thread này cũng là nơi frame kế tiếp xếp hàng.
//     - Không giữ con trỏ/fd trong LinuxFrameInfo sau khi callback trả về
//       (CaptureTypes.h).
//
// LIÊN QUAN: capture/CaptureTypes.h, capture/PortalScreenCast.h (nguồn nodeId + fd),
//            encode/VaEncoder.h (bên tiêu thụ), AgentLoop.cpp
// =============================================================================
#include <cstdint>
#include <functional>
#include <memory>

#include "capture/CaptureTypes.h"

class ScreenCapture {
public:
    // Gọi trên thread của PipeWire mỗi khi có frame MỚI. Phải xử lý nhanh và KHÔNG
    // giữ con trỏ/fd sau khi trả về.
    using FrameHandler = std::function<void(const LinuxFrameInfo&)>;

    ScreenCapture();
    ~ScreenCapture();
    ScreenCapture(const ScreenCapture&) = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;

    // Bắt đầu bắt hình node `nodeId` trên kết nối PipeWire `portalFd`.
    //
    // `portalFd` thuộc về PortalScreenCast và KHÔNG bị lớp này lấy mất: bên trong
    // tự dup() một bản riêng, vì pw_context_connect_fd() đóng fd nó nhận. Nhờ vậy
    // nhiều ScreenCapture cùng dùng được một fd của portal.
    //
    // `fps` là TRẦN tốc độ khung ta xin; compositor có thể phát chậm hơn (màn hình
    // tĩnh) nhưng sẽ không phát nhanh hơn.
    //
    // Trả về ngay sau khi khởi động thread loop, KHÔNG chờ frame đầu (khác bản
    // macOS): trên Wayland việc thoả thuận định dạng mất vài trăm ms và AgentLoop
    // đằng nào cũng có sẵn vòng đợi frame đầu.
    // false = không dựng được stream.
    bool Start(int portalFd, uint32_t nodeId, uint32_t fps, FrameHandler onFrame);
    void Stop();

    // True khi node đã biến mất (người dùng bấm "Stop sharing" trên chỉ báo của
    // compositor, hoặc màn hình bị rút) hoặc stream báo lỗi không hồi phục được.
    // AgentLoop dùng nó để gỡ nguồn khỏi phiên.
    bool Closed() const;

    // Đường bộ nhớ đang dùng, để log/chẩn đoán biết mình có đang zero-copy không.
    // Chỉ có ý nghĩa sau khi frame đầu về.
    bool usingDmaBuf() const;

    // Trạng thái riêng, định nghĩa trong .cpp.
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};
