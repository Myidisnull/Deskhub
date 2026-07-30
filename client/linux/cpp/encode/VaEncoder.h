#pragma once
// =============================================================================
// VaEncoder.h — nén H.264 bằng bộ mã hoá phần cứng của GPU qua VA-API.
//               Đối ứng client/windows/cpp/encode/NvencEncoder.h và
//               client/macos/.../encode/VtEncoder.h.
//
// NHIỆM VỤ
//   Nhận LinuxFrameInfo (RGB, dma-buf hoặc RAM) từ ScreenCapture và trả về NAL
//   Annex-B qua callback. Đây là mắt xích rủi ro nhất của vai host (docs/02 §3) —
//   cũng như trên hai nền tảng kia.
//
// VỊ TRÍ TRONG LUỒNG DỮ LIỆU
//   ScreenCapture → **VaEncoder** → Packetizer → UDP ~~~> Reassembler → AvDecoder
//
// ĐƯỜNG MÀU: RGB → NV12 TRÊN GPU
//   Compositor Wayland đưa ra bộ đệm màn hình ở dạng RGB 32-bit; H.264 cần NV12.
//   Phép đổi đó chạy bằng VPP (VAEntrypointVideoProc) NGAY TRÊN GPU, không phải
//   trên CPU:
//        dma-buf RGB ──import──► VA surface RGB ──VPP──► VA surface NV12 ──encode──►
//   Bản Windows cũng làm đúng thế bằng D3D11 Video Processor; bản macOS thì không
//   cần vì ScreenCaptureKit phát thẳng NV12.
//
// ⚠ VÌ SAO PHẢI TỰ VIẾT SPS/PPS
//   VA-API mã hoá phần slice, nhưng bộ tham số thì mỗi driver một kiểu và không
//   driver nào lặp lại chúng ở mỗi IDR — trong khi giao thức Deskhub đòi đúng điều
//   đó (client vào xem giữa chừng không có đường nào khác để lấy tham số). Nên ta
//   dựng byte SPS/PPS bằng BitWriter rồi đưa cho driver dưới dạng "packed header".
//   Hệ quả: các hằng số cấu hình chuỗi (số bit frame_num, POC...) phải dùng CHUNG
//   giữa hai nơi — đó là lý do chúng nằm ngay dưới đây thay vì rải trong .cpp.
//   Đọc kỹ BitWriter.h trước khi sửa bất kỳ hằng nào.
//
// ⚠ QUY TẮC VÒNG ĐỜI CỦA onPacket — KHÁC macOS
//   VideoToolbox nén BẤT ĐỒNG BỘ và gọi callback trên thread riêng của nó. VA-API
//   thì đồng bộ: Encode() nộp frame, chờ vaSyncSurface, đọc bitstream rồi gọi
//   onPacket NGAY TRÊN THREAD GỌI. Nghĩa là onPacket chạy trên thread PipeWire
//   (đường capture) hoặc thread Recv (đường keepalive/IDR tĩnh) — và AgentLoop đã
//   giữ encMutex quanh cả hai, nên các lời gọi onPacket vẫn NỐI TIẾP đúng thứ tự,
//   điều Packetizer đòi hỏi (nó single-thread, không tự khoá). Vẫn giữ hai ràng
//   buộc cũ: `data` chỉ hợp lệ trong phạm vi lời gọi, và không làm việc chậm bên
//   trong.
//
// CẤU HÌNH LOW-LATENCY (docs/02 §3)
//   Không B-frame (ip_period = 1), GOP VÔ HẠN + IDR theo yêu cầu (intra_period =
//   0), CBR, đúng MỘT khung tham chiếu. Một reference duy nhất là chủ ý: mất gói
//   trên UDP thì càng ít frame phụ thuộc lẫn nhau càng dễ hồi.
//
// LIÊN QUAN: capture/CaptureTypes.h, encode/VaDisplay.h, encode/BitWriter.h,
//            AgentLoop.cpp (người dùng), client/macos/.../encode/VtEncoder.h
// =============================================================================
#include <va/va.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "capture/CaptureTypes.h"

// Nhận một frame H.264 Annex-B vừa nén xong. Chạy trên thread gọi Encode().
// `data` chỉ hợp lệ trong phạm vi callback.
using PacketHandler = std::function<void(const uint8_t* data, size_t size,
    uint64_t timestampUs, bool keyframe)>;

struct EncoderConfig {
    uint32_t width = 0;  // phải CHẴN (ScreenCapture đã lo)
    uint32_t height = 0; // phải CHẴN
    uint32_t fps = 60;
    uint32_t bitrateBps = 20'000'000;
    PacketHandler onPacket;
};

// --- Hằng số cấu hình chuỗi H.264 ---
// Dùng CHUNG giữa VAEncSequenceParameterBufferH264 (nói cho driver) và BitWriter
// (viết SPS cho decoder). Lệch nhau = client giải ra rác từ frame thứ hai.
inline constexpr uint32_t kLog2MaxFrameNumMinus4 = 12; // frame_num quay vòng ở 2^16
inline constexpr uint32_t kLog2MaxPocLsbMinus4 = 12;   // POC lsb quay vòng ở 2^16
inline constexpr uint32_t kMaxRefFrames = 1;           // đúng một khung tham chiếu

// pic_init_qp của PPS. PHẢI khớp pic_init_qp_minus26 = 0 mà BuildParameterSets ghi:
// QP mỗi frame được điều bằng slice_qp_delta TÍNH TỪ mốc này, và bộ giải mã đọc
// mốc đó từ PPS. Đổi một bên mà quên bên kia là lệch QP toàn bộ chuỗi.
inline constexpr int kPicInitQp = 26;

class VaEncoder {
public:
    VaEncoder() = default;
    ~VaEncoder();
    VaEncoder(const VaEncoder&) = delete;
    VaEncoder& operator=(const VaEncoder&) = delete;

    // false = không có GPU mã hoá được, kích thước vô lý, hoặc driver từ chối.
    bool Init(const EncoderConfig& cfg);

    // Nén một frame. `fi` chỉ cần sống qua lời gọi này (dma-buf được import và
    // huỷ ngay trong hàm). `forceKeyframe` xin IDR — dùng khi client mất gói hoặc
    // vừa vào phiên.
    bool Encode(const LinuxFrameInfo& fi, uint64_t timestampUs, bool forceKeyframe);

    // Nén LẠI frame nguồn gần nhất, không cần frame mới từ capture.
    //
    // ⚠ ĐÂY LÀ BẢN LINUX CỦA "CACHE FRAME CUỐI". Compositor chỉ phát frame khi
    // nội dung ĐỔI, nên nguồn đang tĩnh (menu đứng im) mà client xin IDR thì
    // không có gì để nén — không có hàm này thì client vào xem một màn hình tĩnh
    // sẽ ĐEN VĨNH VIỄN. Bản Windows/macOS giải quyết bằng cách giữ một bản sao
    // frame cuối; ở đây rẻ hơn nhiều vì surface NV12 đầu ra của VPP VẪN CÒN
    // NGUYÊN trong VRAM từ lần Encode trước — không phải chép gì cả.
    //
    // Cũng là đường của KEEPALIVE ~2fps (AgentLoop.cpp): giữ đồng hồ trình bày
    // của client chạy khi màn hình đứng im.
    // false = chưa có frame nào từng được nén.
    bool EncodeLast(uint64_t timestampUs, bool forceKeyframe);

    bool haveSourceFrame() const {
        return haveSource_;
    }

    // GĐ5: đổi bitrate mục tiêu giữa chừng (congestion control theo FEEDBACK).
    // Không dựng lại context — chuỗi inter-frame giữ nguyên, không cần IDR. Thực
    // tế việc đổi được ÁP ở frame kế tiếp (VA-API nhận tham số rate control kèm
    // theo một lần nộp frame, không có API đổi tức thì).
    // false = kích thước vô lý; driver từ chối thì lộ ra ở lần Encode sau.
    bool SetBitrate(uint32_t bitrateBps);

    // Đóng context + giải phóng surface. Gọi được nhiều lần.
    void Finish();

    bool IsOpen() const {
        return encContext_ != VA_INVALID_ID;
    }
    const char* BackendName() const {
        return "VA-API (hardware)";
    }
    // Đường bộ nhớ của frame gần nhất — để log chẩn đoán biết có đang zero-copy không.
    bool lastFrameWasDmaBuf() const {
        return lastDmaBuf_;
    }
    // QP đang dùng khi driver chỉ có CQP; 0 nghĩa là driver tự điều tiết và QP
    // không phải núm của ta. Cho dòng DIAG (docs/09): QP dán trần là dấu hiệu
    // bitrate mục tiêu thấp hơn mức encoder với tới được ở độ phân giải này.
    int currentQp() const {
        return cqpMode_ ? qp_ : 0;
    }

private:
    bool CreateContexts();
    void BuildParameterSets();
    // QP cho IDR kế tiếp ở chế độ CQP: chọn để vừa hạn mức burst, dự đoán từ chính
    // IDR trước đó. Xem "IDR CÓ HẠN MỨC RIÊNG" ở .cpp.
    int IdrQp() const;
    // Import dma-buf thành VA surface RGB. VA_INVALID_SURFACE = thất bại.
    VASurfaceID ImportDmaBuf(const LinuxFrameInfo& fi);
    // Chép frame trong RAM lên surface RGB dùng lại. false = thất bại.
    bool UploadMapped(const LinuxFrameInfo& fi);
    // RGB surface → NV12 surface bằng VPP.
    bool ConvertToNv12(VASurfaceID rgb);
    // Nộp một frame cho bộ mã hoá và đọc bitstream ra `out_`.
    bool EncodeNv12(bool idr, size_t& outSize);

    VADisplay dpy_ = nullptr;
    EncoderConfig cfg_{};

    // Kích thước đã căn 16 (một macroblock). Surface cấp theo cỡ này; phần dư so
    // với cfg_.width/height được cắt bằng frame_cropping trong SPS.
    uint32_t alignedW_ = 0, alignedH_ = 0;
    uint32_t mbW_ = 0, mbH_ = 0;

    // --- Mã hoá ---
    // Entrypoint VaDisplay đã dò ra (EncSlice hoặc EncSliceLP). Phải dùng ĐÚNG
    // giá trị đó cho cả vaGetConfigAttributes và vaCreateConfig — hỏi năng lực
    // của một entrypoint rồi tạo config bằng entrypoint khác là cách chắc chắn
    // nhận VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT. Xem VaDisplay.h.
    VAEntrypoint encEntrypoint_ = VAEntrypointEncSlice;
    VAConfigID encConfig_ = VA_INVALID_ID;
    VAContextID encContext_ = VA_INVALID_ID;
    VABufferID codedBuf_ = VA_INVALID_ID;
    // Surface NGUỒN của lần nộp (đầu ra của VPP) và HAI surface tái dựng luân
    // phiên (khung hiện tại + khung tham chiếu). Xem "mô hình tham chiếu" ở .cpp.
    VASurfaceID srcNv12_ = VA_INVALID_SURFACE;
    VASurfaceID reconNv12_[2] = {VA_INVALID_SURFACE, VA_INVALID_SURFACE};

    // --- VPP (đổi màu RGB → NV12) ---
    VAConfigID vppConfig_ = VA_INVALID_ID;
    VAContextID vppContext_ = VA_INVALID_ID;

    // --- Nguồn RGB cho đường Mapped (dùng lại, không cấp phát mỗi frame) ---
    VASurfaceID rgbSurface_ = VA_INVALID_SURFACE;
    VAImage rgbImage_{};
    bool haveRgbImage_ = false;
    uint32_t rgbFourcc_ = 0; // VA_FOURCC đang dùng cho rgbSurface_/rgbImage_
    // Cỡ THẬT của khung đang vào. Có thể LỚN HƠN cfg_.width/height — khi đó bước
    // VPP vừa đổi màu vừa CO (xem ConvertToNv12). Phải nhớ riêng vì cfg_ mang cỡ
    // NÉN, còn surface RGB thì dựng theo cỡ khung tới.
    uint32_t srcW_ = 0, srcH_ = 0;

    // --- Trạng thái chuỗi mã hoá ---
    uint64_t frameCount_ = 0;
    uint32_t frameNum_ = 0;
    int32_t poc_ = 0;
    uint16_t idrPicId_ = 0;
    bool haveRef_ = false;
    uint32_t refFrameNum_ = 0;
    int32_t refPoc_ = 0;
    VASurfaceID refSurface_ = VA_INVALID_SURFACE;

    // Bitrate cần báo cho driver ở lần nộp kế tiếp. 0 = không có gì đổi.
    uint32_t pendingBitrate_ = 0;

    // --- Điều tiết bitrate bằng QP (CHỈ khi driver không có CBR) ---
    // Driver chỉ có CQP thì bitrate mục tiêu là lời nói suông: nó phát ra bao nhiêu
    // bit là do QP cố định quyết định, không phải do ta xin. Vòng điều khiển này
    // biến mục tiêu thành hành động — xem "ĐIỀU TIẾT BITRATE KHI CHỈ CÓ CQP" ở .cpp.
    // cqpMode_ = false thì cả khối này ngủ và driver tự lo.
    bool cqpMode_ = false;
    int qp_ = kPicInitQp; // QP cho P-frame; IDR dùng qp_ + kIdrQpDelta
    // Trung bình trượt của ln(kích thước thật / hạn mức) trên các P-frame. Miền log
    // để không phụ thuộc thang đo, và trượt để lọc nhiễu nội dung.
    double logRatioEma_ = 0.0;
    bool haveRatio_ = false;
    // Mốc thời gian lần nộp frame thành công trước — hạn mức tính theo thời gian
    // THẬT đã trôi, không theo cfg_.fps. Xem lý do ở .cpp.
    uint64_t lastEncodeUs_ = 0;
    // Quan sát của IDR gần nhất, để dự đoán QP cho IDR kế tiếp. 0 = chưa có.
    int lastIdrQp_ = 0;
    size_t lastIdrBytes_ = 0;

    bool packedHeaders_ = false; // driver nhận SPS/PPS của ta?
    bool lastDmaBuf_ = false;
    bool haveSource_ = false; // srcNv12_ đã có nội dung thật (cho EncodeLast)
    std::vector<uint8_t> sps_, pps_;
    uint32_t spsBits_ = 0, ppsBits_ = 0;
    // Bộ đệm gom bitstream, dùng lại để không cấp phát 60 lần mỗi giây.
    std::vector<uint8_t> out_;
};
