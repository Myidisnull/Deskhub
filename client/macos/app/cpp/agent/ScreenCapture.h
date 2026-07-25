#pragma once
// =============================================================================
// ScreenCapture.h — bắt hình một cửa sổ HOẶC một màn hình bằng ScreenCaptureKit.
//                   Đầu nguồn của toàn bộ luồng video. Đối ứng chính xác của
//                   client/windows/capture/WindowCapture.h (WGC).
//
// NHIỆM VỤ
//   Biến một CGWindowID/CGDirectDisplayID thành dòng CVPixelBuffer (NV12) chảy đều
//   đặn ra callback.
//
// VỊ TRÍ TRONG LUỒNG DỮ LIỆU
//   SourceEnum → **ScreenCapture** → VtEncoder → Packetizer → UDP
//
// BỐN QUYẾT ĐỊNH THIẾT KẾ
//   1. THEO SỰ KIỆN, KHÔNG POLLING. SCStream đẩy frame ra delegate; ta chỉ xử lý
//      frame có SCFrameStatusComplete (nội dung THẬT SỰ đổi) và bỏ SCFrameStatusIdle.
//      Đây chính là hành vi "WGC chỉ phát frame khi nội dung đổi" bên Windows — nên
//      cơ chế CACHE FRAME CUỐI ở AgentLoop vẫn cần y nguyên, vì cùng một lý do:
//      nguồn đứng im thì không có gì để nén khi client xin IDR.
//
//   2. PIMPL ĐỂ GIẤU ScreenCaptureKit. Toàn bộ Objective-C nằm trong .mm. Header này
//      chỉ lộ C++ thuần + void*, nên AgentLoop.cpp là .cpp bình thường (xem
//      CaptureTypes.h).
//
//   3. NV12 THẲNG TỪ NGUỒN. Cấu hình stream ở pixel format '420v' để VideoToolbox
//      nhận đúng thứ nó muốn — không có bước đổi màu nào trên đường nóng. Bản Windows
//      phải qua video processor để đổi BGRA→NV12; ở đây khỏi.
//
//   4. KÍCH THƯỚC LUÔN CHẴN, VÀ TỰ THEO DÕI ĐỔI CỠ. Ta làm tròn xuống số chẵn khi
//      cấu hình. Người dùng kéo cửa sổ to nhỏ thì SCStream KHÔNG tự đổi kích thước
//      buffer — nó co nội dung vào khung cũ. Nên .mm chạy một bộ đếm 500ms tự so
//      kích thước nguồn và gọi updateConfiguration khi lệch; frame sau đó về với cỡ
//      mới và AgentLoop thấy nó qua đúng đường "sizeChanged" như bản Windows.
//
// ⚠ CALLBACK CHẠY TRÊN QUEUE CỦA SCStream
//   Hai hệ quả bắt buộc phải nhớ:
//     - Phải xử lý NHANH. Queue này cũng là nơi frame kế tiếp xếp hàng.
//     - Không giữ MacFrameInfo::pixelBuffer sau khi callback trả về (CaptureTypes.h).
//
// LIÊN QUAN: agent/CaptureTypes.h, agent/VtEncoder.h (bên tiêu thụ),
//            agent/AgentLoop.cpp, client/windows/capture/WindowCapture.h
// =============================================================================
#include <functional>
#include <memory>

#include "agent/CaptureTypes.h"

class ScreenCapture {
public:
    // Gọi trên queue riêng của SCStream mỗi khi có frame MỚI (nội dung đổi).
    // Phải xử lý nhanh và KHÔNG giữ pixelBuffer sau khi trả về.
    using FrameHandler = std::function<void(const MacFrameInfo&)>;

    ScreenCapture();
    ~ScreenCapture();
    ScreenCapture(const ScreenCapture&) = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;

    // Bắt đầu bắt hình `target`, gọi `onFrame` cho mỗi frame mới. CHẶN tới ~2 giây
    // (phải hỏi SCShareableContent để tìm đối tượng SCWindow/SCDisplay) → gọi ngoài
    // main thread. false = không tìm thấy nguồn, thiếu quyền, hoặc stream không khởi
    // động được.
    bool Start(const CaptureTarget& target, uint32_t fps, FrameHandler onFrame);
    void Stop();

    // True khi nguồn mục tiêu đã biến mất (cửa sổ đóng, màn hình bị rút) hoặc
    // SCStream báo lỗi không hồi phục được. AgentLoop dùng nó để gỡ nguồn khỏi phiên.
    bool Closed() const;

    // Trạng thái riêng, định nghĩa trong .mm. Khai báo ở phần PUBLIC vì lớp
    // Objective-C nhận frame (DeskhubStreamOutput) phải giữ được con trỏ tới nó —
    // một lớp Obj-C không thể là bạn (friend) của lớp C++.
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};
