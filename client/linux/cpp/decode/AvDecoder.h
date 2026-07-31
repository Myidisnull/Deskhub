#pragma once
// =============================================================================
// AvDecoder.h — giải mã H.264 bằng libavcodec, tăng tốc phần cứng qua VA-API.
//               Đối ứng client/windows/cpp/decode/MfDecoder.h,
//               client/macos/.../decode/VtDecoder.h,
//               client/android/.../MediaCodecDecoder.
//
// NHIỆM VỤ
//   Nhận frame H.264 Annex-B đã ghép đủ (từ Reassembler) và trả về một AVFrame
//   để tầng vẽ đưa lên màn hình.
//
// ⚠ VÌ SAO libavcodec Ở ĐÂY, TRONG KHI PHÍA MÃ HOÁ LÀ VA-API TRẦN
//   Hai chiều KHÔNG đối xứng nhau về khối lượng, và đây là lý do:
//     - MÃ HOÁ bằng VA-API trần chỉ cần điền vài struct: ta tự quyết định cấu
//       trúc GOP, ta biết trước mọi tham số, và ta chỉ phải TỰ VIẾT SPS/PPS.
//     - GIẢI MÃ bằng VA-API trần thì VASliceParameterBufferH264 đòi ta phải tự
//       phân tích SPS/PPS/slice header, tự tính POC, tự quản DPB và ref-pic-list
//       — tức là viết nửa bộ giải mã H.264. Khối lượng và rủi ro sai lệch hơn một
//       bậc, để đổi lấy đúng thứ libavcodec đã làm sẵn và đã được kiểm chứng
//       nhiều năm.
//   libavcodec ở đây KHÔNG giải mã bằng CPU: hwaccel VA-API vẫn chạy trên GPU,
//   libavcodec chỉ lo phần phân tích cú pháp và quản lý DPB. Đường lùi phần mềm
//   chỉ bật khi máy không có VA-API giải mã được.
//
// ĐỊNH DẠNG BITSTREAM
//   Stream của Deskhub là Annex-B (start code), IDR mang sẵn SPS/PPS in-band
//   (encode/VaEncoder.h). Đó cũng là thứ libavcodec nhận trực tiếp — không cần
//   lớp chuyển đổi nào, khác hẳn VtDecoder bên macOS/iOS phải đổi Annex-B → AVCC.
//
// ⚠ CẤU HÌNH ĐỘ TRỄ THẤP LÀ BẮT BUỘC
//   Mặc định libavcodec đệm vài frame để chạy đa luồng và để xử lý B-frame. Với
//   stream này thì cả hai đều là độ trễ thuần tuý: ta không có B-frame, và một
//   frame nằm chờ trong bộ đệm là một frame người dùng chưa thấy. AV_CODEC_FLAG_
//   LOW_DELAY + thread_type = SLICE + thread_count = 1 tắt hết phần đệm đó.
//
// MÔ HÌNH LUỒNG
//   Dùng trên MỘT thread (thread Decode). Init/Shutdown/Decode phải cùng thread
//   đó. Frame ra được giao cho VideoSink — xem render/VideoSink.h về quyền sở hữu.
//
// LIÊN QUAN: ClientLoop.h (chủ sở hữu + luồng Decode), render/VideoSink.h,
//            render/VideoRenderer.h (bên tiêu thụ)
// =============================================================================
#include <cstddef>
#include <cstdint>

#include "deskhub/media/VideoContract.h"

class VideoSink;

class AvDecoder {
public:
    AvDecoder() = default;
    ~AvDecoder();
    AvDecoder(const AvDecoder&) = delete;
    AvDecoder& operator=(const AvDecoder&) = delete;

    // `sink` phải sống lâu hơn decoder. width/height chỉ để log và để gợi ý kích
    // thước ban đầu; kích thước thật lấy từ SPS trong stream.
    bool Init(VideoSink* sink, int width, int height);
    void Shutdown();
    bool IsOpen() const {
        return ctx_ != nullptr;
    }

    // Nạp một frame Annex-B đã ghép đủ. Frame giải ra được đẩy thẳng vào sink.
    // false = lỗi -> caller dựng lại decoder và xin IDR, y như đường lỗi của
    // MfDecoder/VtDecoder.
    bool Decode(const uint8_t* nal, size_t len, uint64_t ptsUs);

    // true nếu đang chạy hwaccel VA-API; false = đang giải mã bằng CPU.
    bool hardware() const {
        return hwDevice_ != nullptr;
    }

private:
    // Kiểu mờ để header này không kéo theo libavcodec vào ClientLoop.cpp.
    void* ctx_ = nullptr;      // AVCodecContext*
    void* packet_ = nullptr;   // AVPacket*
    void* frame_ = nullptr;    // AVFrame*
    void* hwDevice_ = nullptr; // AVBufferRef* (AVHWDeviceContext, VA-API)
    VideoSink* sink_ = nullptr;
};

// Hợp đồng chữ ký, ép lúc BIÊN DỊCH (deskhub/media/VideoContract.h). Bốn viewer
// (Apple, Android, Ubuntu) phải nói cùng một thứ tiếng ở bốn hàm này — lệch là gãy
// build ngay tại dòng này chứ không phải một lỗi lạ trên máy người dùng.
static_assert(deskhub::media::VideoDecoderLike<AvDecoder>,
    "AvDecoder phải giữ đúng chữ ký chung của bộ giải nén");
static_assert(deskhub::media::RestartableDecoder<AvDecoder>,
    "AvDecoder phải dựng lại được tại chỗ (Shutdown + IsOpen)");
