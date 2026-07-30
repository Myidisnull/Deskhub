#pragma once
// =============================================================================
// ScreenCapture.h — bắt hình MỘT MÀN HÌNH bằng ScreenCaptureKit. Đầu nguồn của
//                   toàn bộ luồng video. Đối ứng chính xác của
//                   client/windows/capture/ScreenCapture.h (WGC). (Đường bắt theo
//                   cửa sổ đã bỏ 2026-07-27 cùng share theo cửa sổ.)
//
// NHIỆM VỤ
//   Biến một CGDirectDisplayID thành dòng CVPixelBuffer (NV12) chảy đều đặn ra
//   callback.
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
//      cấu hình. Đổi độ phân giải/scale màn hình thì SCStream KHÔNG tự đổi kích
//      thước buffer — nó co nội dung vào khung cũ. Nên .mm chạy một bộ đếm 500ms tự
//      so kích thước nguồn và gọi updateConfiguration khi lệch; frame sau đó về với
//      cỡ mới và AgentLoop thấy nó qua đúng đường "sizeChanged" như bản Windows.
//
//   5. TRẦN ĐỘ PHÂN GIẢI (`maxDim`). Màn Retina cho ra khung KHỔNG LỒ: một MacBook
//      Pro 14" là 3024×1964 = 5.9 Mpixel, gấp gần ba lần 1080p, và màn XDR 6K thì
//      gấp bảy. Ở 60fps đó là 356 Mpixel/s đổ vào VideoToolbox, và ở 20 Mbps mặc
//      định thì chỉ còn 0.06 bit/pixel — encoder nghẹt, mỗi IDR phình ra vài trăm
//      KB bắn thành gần nghìn datagram liên tiếp làm tràn buffer gửi, mất gói,
//      BitrateController tụt rate, hình càng nát. Nên ta CO NGAY TỪ NGUỒN: đặt cỡ
//      buffer của SCStream nhỏ hơn và để WindowServer co trên GPU — miễn phí, và
//      encoder chỉ còn thấy đúng số pixel ta muốn trả tiền.
//
//      Co ở đây KHÔNG đụng tới chuột: toạ độ trên wire là chuẩn hoá 0..65535 rồi
//      map qua CGDisplayBounds (input/InputInjector.mm), không dính gì cỡ khung.
//
// ⚠ CALLBACK CHẠY TRÊN QUEUE CỦA SCStream
//   Hai hệ quả bắt buộc phải nhớ:
//     - Phải xử lý NHANH. Queue này cũng là nơi frame kế tiếp xếp hàng.
//     - Không giữ MacFrameInfo::pixelBuffer sau khi callback trả về (CaptureTypes.h).
//
// LIÊN QUAN: capture/CaptureTypes.h, encode/VtEncoder.h (bên tiêu thụ),
//            AgentLoop.cpp, client/windows/capture/ScreenCapture.h
// =============================================================================
#include <functional>
#include <memory>

#include "capture/CaptureTypes.h"

class ScreenCapture {
public:
    // Gọi trên queue riêng của SCStream mỗi khi có frame MỚI (nội dung đổi).
    // Phải xử lý nhanh và KHÔNG giữ pixelBuffer sau khi trả về.
    using FrameHandler = std::function<void(const MacFrameInfo&)>;

    ScreenCapture();
    ~ScreenCapture();
    ScreenCapture(const ScreenCapture&) = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;

    // Bắt đầu bắt hình màn hình `displayId`, gọi `onFrame` cho mỗi frame mới. CHẶN
    // tới ~2 giây (phải hỏi SCShareableContent để tìm đối tượng SCDisplay) → gọi
    // ngoài main thread. false = không tìm thấy màn hình, thiếu quyền, hoặc stream
    // không khởi động được.
    //
    // `maxDim` = trần cho CẠNH DÀI của khung, giữ nguyên tỉ lệ (quyết định 5).
    // 0 = không co, bắt đúng độ phân giải native của màn hình.
    bool Start(uint32_t displayId, uint32_t fps, uint32_t maxDim, FrameHandler onFrame);
    void Stop();

    // Client vừa HELLO và báo màn hình nó `clientW`×`clientH` pixel (0×0 = không
    // biết, hoặc client vừa rời đi). Co luồng cho vừa cả nó lẫn `maxDim` và trả cỡ
    // buffer sau khi tính ra `outW`/`outH` — AgentLoop chào ĐÚNG cỡ này trong
    // HELLO_ACK, nên client dựng bộ giải mã đúng một lần.
    //
    // An toàn gọi từ thread khác thread capture (thực tế là thread Recv). Đổi cỡ
    // thật sự chỉ xảy ra khi kết quả khác cỡ đang chạy; gọi lại với cùng số là no-op.
    void SetClientSize(uint32_t clientW, uint32_t clientH, uint32_t& outW, uint32_t& outH);

    // Áp một bậc của thang chất lượng (deskhub::QualityLadder): `scalePct` co thêm
    // trên cỡ đã qua hai trần ở trên, `fps` là trần khung hình mới.
    //
    // ⚠ HAI THỨ NÀY ĐI CÙNG NHAU, KHÔNG TÁCH RA ĐƯỢC. Cả hai đều nằm trong CÙNG một
    //   SCStreamConfiguration, nên đổi riêng lẻ vẫn tốn đúng một lần
    //   updateConfiguration; tách thành hai API chỉ tạo ra khả năng gọi nửa vời và
    //   để capture chạy ở một bậc không tồn tại trên thang.
    //
    // Trả cỡ buffer sau khi tính qua `outW`/`outH` — AgentLoop cần nó để dựng
    // RECONFIG. An toàn gọi từ thread Recv.
    void SetQuality(uint32_t scalePct, uint32_t fps, uint32_t& outW, uint32_t& outH);

    // Số frame SCStream giao nhưng báo "không có nội dung mới", kể từ lần gọi trước.
    // Đọc cùng `capture N fps` thì phân biệt được "màn hình đứng yên thật" với
    // "macOS ngừng gọi ta" — xem Impl::idleFrames trong .mm.
    uint32_t TakeIdleCount();

    // True khi màn hình mục tiêu đã biến mất (bị rút / đổi cấu hình) hoặc SCStream
    // báo lỗi không hồi phục được. AgentLoop dùng nó để gỡ nguồn khỏi phiên.
    bool Closed() const;

    // Trạng thái riêng, định nghĩa trong .mm. Khai báo ở phần PUBLIC vì lớp
    // Objective-C nhận frame (DeskhubStreamOutput) phải giữ được con trỏ tới nó —
    // một lớp Obj-C không thể là bạn (friend) của lớp C++.
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};
