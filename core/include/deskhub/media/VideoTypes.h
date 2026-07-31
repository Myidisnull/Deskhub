#pragma once
// =============================================================================
// VideoTypes.h — TỪ VỰNG chung của tầng nén/giải nén, một bản cho cả năm nền.
//
// NHIỆM VỤ
//   Giữ những kiểu mà CẢ hai đầu của đường video đều nói tới: mã hoá gì, kích
//   thước bao nhiêu, bitrate bao nhiêu, NAL nén xong đi đâu. Đây là TỪ VỰNG, không
//   phải hợp đồng gọi hàm — hợp đồng nằm ở deskhub/media/VideoContract.h.
//
// ⚠ VÌ SAO NẰM Ở CORE
//   Trước 31/07/2026, `EncoderConfig` và `PacketHandler` được định nghĩa BA LẦN
//   (Windows/macOS/Ubuntu), và hai bản macOS + Ubuntu giống nhau ĐẾN TỪNG BYTE.
//   Thêm một núm điều chỉnh cho encoder nghĩa là sửa ba struct và nhớ giữ chúng
//   khớp — mà chúng đã không khớp: bản Windows có srcWidth/srcHeight, rc,
//   lowLatency, còn hai bản kia không, nên cùng một `EncoderConfig` lại mang ý
//   nghĩa khác nhau tuỳ file đang mở.
//
// KHÔNG CÓ GÌ CỦA HỆ ĐIỀU HÀNH Ở ĐÂY
//   Không texture, không CVPixelBuffer, không dma-buf. Handle của frame là biên
//   giới nền tảng THẬT và nó ở lại từng client (capture/CaptureTypes.h). Nhờ vậy
//   file này build được bằng mọi toolchain và test được offline như phần còn lại
//   của core.
//
// LIÊN QUAN: deskhub/media/VideoContract.h (hợp đồng chữ ký encoder/decoder),
//            client/*/encode/*.h, client/*/decode/*.h (nơi cài đặt)
// =============================================================================
#include <cstddef>
#include <cstdint>
#include <functional>

namespace deskhub::media {

enum class Codec { H264,
    HEVC };

enum class RateControl { CBR,
    VBR };

// Nhận một gói NAL Annex-B vừa nén xong (1 frame).
//
// ⚠ QUY TẮC VÒNG ĐỜI — ĐỌC TRƯỚC KHI VIẾT MỘT CÀI ĐẶT MỚI
//   Callback chạy ĐỒNG BỘ trên chính thread gọi Encode(), và `data` chỉ hợp lệ
//   trong phạm vi lời gọi. Hai hệ quả:
//     - Phải copy hoặc tiêu thụ ngay, không giữ con trỏ lại.
//     - Không được làm việc chậm trong đó. Encode() thường được gọi từ callback
//       của tầng capture (FrameArrived của WGC, handler của ScreenCaptureKit,
//       vòng PipeWire), nên ngủ ở đây là làm đứng cả đường bắt hình.
using PacketHandler =
    std::function<void(const uint8_t* data, size_t size, uint64_t timestampUs, bool keyframe)>;

// Cấu hình một phiên nén.
//
// VÌ SAO CÓ CẢ width/height LẪN srcWidth/srcHeight
//   H.264 dùng NV12, lấy mẫu chroma theo khối 2×2, nên kích thước NÉN bắt buộc
//   phải CHẴN. Nhưng cửa sổ người dùng chọn có thể rộng hoặc cao lẻ. Khi đó ta nén
//   ở kích thước chẵn nhỏ hơn một điểm ảnh, còn texture đưa vào vẫn giữ kích thước
//   lẻ thật — bộ xử lý video cần biết CẢ HAI để cắt cho đúng thay vì co giãn méo.
//   Bằng nhau thì để srcWidth/srcHeight = 0.
//
//   Trên macOS và Ubuntu hai trường này luôn là 0: tầng capture ở đó đã làm tròn
//   xuống số chẵn trước khi giao frame. Giữ chúng trong struct dùng chung vẫn đúng
//   — chúng mô tả một tình huống có thật của bài toán, chỉ là nền khác giải quyết
//   ở chỗ khác.
struct EncoderConfig {
    Codec codec = Codec::H264;
    uint32_t width = 0;  // kích thước NÉN — phải CHẴN
    uint32_t height = 0;
    uint32_t srcWidth = 0;  // kích thước texture đầu vào THẬT (0 = bằng width)
    uint32_t srcHeight = 0;
    uint32_t fps = 60;
    uint32_t bitrateBps = 20'000'000;
    RateControl rc = RateControl::CBR;
    bool lowLatency = true;
    PacketHandler onPacket;
};

struct DecoderConfig {
    Codec codec = Codec::H264;
    uint32_t width = 0; // kích thước gợi ý; decoder tự đọc lại từ SPS khi stream đổi
    uint32_t height = 0;
    uint32_t fps = 60;
};

} // namespace deskhub::media
