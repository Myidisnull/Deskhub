#pragma once
// =============================================================================
// VtEncoder.h — nén H.264 bằng bộ mã hoá phần cứng của macOS (VideoToolbox).
//               Đối ứng client/windows/encode/IVideoEncoder.h + NvencEncoder.
//
// NHIỆM VỤ
//   Nhận CVPixelBuffer (NV12) từ ScreenCapture và trả về NAL Annex-B qua callback.
//   Đây là mắt xích rủi ro nhất của vai host (docs/02 §3) — cũng như bên Windows.
//
// VỊ TRÍ TRONG LUỒNG DỮ LIỆU
//   ScreenCapture → **VtEncoder** → Packetizer → UDP ~~~> Reassembler → VtDecoder
//
// VÌ SAO KHÔNG CÓ INTERFACE TRỪU TƯỢNG NHƯ IVideoEncoder BÊN WINDOWS
//   Windows cần lớp trừu tượng vì có tới bốn backend tuỳ theo GPU (NVENC/AMF/QSV/MF)
//   và phải thử lần lượt. macOS chỉ có MỘT đường: VideoToolbox, chạy trên mọi máy
//   Mac từ Intel Quick Sync tới Apple Silicon Media Engine. Thêm một lớp ảo cho một
//   bản cài đặt duy nhất là chi phí không đổi lấy gì.
//
// ⚠ ĐỊNH DẠNG BITSTREAM: AVCC → ANNEX-B (đây là chỗ dễ sai nhất)
//   VideoToolbox XUẤT ra AVCC: mỗi NAL có tiền tố ĐỘ DÀI 4 byte, còn SPS/PPS nằm
//   ngoài luồng dữ liệu, trong CMVideoFormatDescription. Giao thức Deskhub thì đòi
//   Annex-B (start code 00 00 00 01) với SPS/PPS ĐI KÈM MỖI IDR — đúng như NVENC bật
//   repeatSPSPPS (docs/08 §3). Nên .mm phải làm hai việc cho mỗi frame ra:
//     1. Đổi mọi tiền tố độ dài thành start code.
//     2. Với frame IDR, chèn SPS + PPS (lấy từ format description) vào TRƯỚC.
//   Bỏ bước 2 thì client kết nối giữa chừng sẽ không bao giờ giải mã được — nó không
//   có tham số, và VtDecoder bỏ mọi frame cho tới khi thấy SPS/PPS.
//
// ⚠ QUY TẮC VÒNG ĐỜI CỦA onPacket
//   KHÁC bản Windows một điểm quan trọng: callback KHÔNG chạy trên thread gọi
//   Encode(). VideoToolbox trả kết quả bất đồng bộ trên thread nội bộ của nó. Ta
//   khoá quanh phần thân callback nên các lời gọi onPacket vẫn NỐI TIẾP và đúng thứ
//   tự — điều Packetizer đòi hỏi (nó là single-thread, không tự khoá). Vẫn giữ
//   nguyên hai ràng buộc cũ: `data` chỉ hợp lệ trong phạm vi lời gọi, và không được
//   làm việc chậm bên trong.
//
// CẤU HÌNH LOW-LATENCY BẮT BUỘC (docs/02 §3)
//   RealTime = true, AllowFrameReordering = false (không B-frame), GOP vô hạn +
//   IDR theo yêu cầu, CBR-ish qua DataRateLimits. Chi tiết từng núm ở .mm.
//
// LIÊN QUAN: agent/CaptureTypes.h, agent/AgentLoop.cpp (người dùng),
//            client/windows/encode/NvencEncoder.h (bản song song),
//            client/macos/app/cpp/client/VtDecoder.h (đầu giải mã)
// =============================================================================
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

// Nhận một frame H.264 Annex-B vừa nén xong. Chạy trên thread nội bộ của
// VideoToolbox, nối tiếp nhau. `data` chỉ hợp lệ trong phạm vi callback.
using PacketHandler = std::function<void(const uint8_t* data, size_t size,
    uint64_t timestampUs, bool keyframe)>;

struct EncoderConfig {
    uint32_t width = 0;  // phải CHẴN (ScreenCapture đã lo)
    uint32_t height = 0; // phải CHẴN
    uint32_t fps = 60;
    uint32_t bitrateBps = 20'000'000;
    PacketHandler onPacket;
};

class VtEncoder {
public:
    VtEncoder() = default;
    ~VtEncoder();
    VtEncoder(const VtEncoder&) = delete;
    VtEncoder& operator=(const VtEncoder&) = delete;

    // false = không dựng được session (kích thước vô lý, không có bộ mã hoá).
    bool Init(const EncoderConfig& cfg);

    // Nén một frame. `pixelBuffer` là CVPixelBufferRef từ ScreenCapture; nó chỉ cần
    // sống qua lời gọi này (VideoToolbox tự giữ tham chiếu nếu cần).
    // `forceKeyframe` xin IDR — dùng khi client mất gói hoặc vừa vào phiên.
    bool Encode(void* pixelBuffer, uint64_t timestampUs, bool forceKeyframe);

    // GĐ5: đổi bitrate mục tiêu giữa chừng (congestion control theo FEEDBACK).
    // Không dựng lại session — chuỗi inter-frame giữ nguyên, không cần IDR.
    // false = VideoToolbox từ chối, caller cứ chạy tiếp với bitrate cũ.
    bool SetBitrate(uint32_t bitrateBps);

    // Đẩy nốt frame còn trong session rồi đóng. Gọi được nhiều lần.
    void Finish();

    bool IsOpen() const {
        return session_ != nullptr;
    }
    const char* BackendName() const {
        return hardware_ ? "VideoToolbox (hardware)" : "VideoToolbox (software)";
    }

    // ⚠ CHỈ dành cho callback của VideoToolbox trong VtEncoder.mm — đừng gọi từ đâu
    // khác. Public (thay vì friend) vì chữ ký thật của callback mang kiểu
    // CMSampleBufferRef, mà header này phải giữ C++ thuần để AgentLoop.cpp include
    // được; khai báo friend với kiểu đã bị xoá thành void* thì không khớp.
    // `sampleBuffer` là CMSampleBufferRef.
    void OnEncoded(void* sampleBuffer, int32_t status, uint32_t infoFlags);

private:
    void* session_ = nullptr; // VTCompressionSessionRef
    EncoderConfig cfg_{};
    bool hardware_ = false;

    // Nối tiếp hoá các lời gọi onPacket (xem quy tắc vòng đời ở đầu file) và bảo vệ
    // annexb_ — bộ đệm dùng lại để không cấp phát 60 lần mỗi giây trên đường nóng.
    std::mutex emitMutex_;
    std::vector<uint8_t> annexb_;
};
