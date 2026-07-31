#pragma once
// =============================================================================
// IVideoEncoder.h — giao diện trừu tượng cho mọi backend nén video.
//
// NHIỆM VỤ
//   Tách phần gọi encoder khỏi backend cụ thể (NVENC / Media Foundation / sau này
//   có thể thêm cái khác). Nhờ vậy AgentLoop viết một lần và chạy được trên máy
//   NVIDIA, Intel, AMD, hay thậm chí không có GPU — đúng chuỗi ưu tiên phần cứng
//   mà GpuSelect dựng ra.
//
// VỊ TRÍ TRONG LUỒNG DỮ LIỆU
//   ScreenCapture → **IVideoEncoder** → Packetizer → Pacer → UDP
//   Vào là texture D3D11 trong VRAM, ra là NAL Annex-B qua callback onPacket.
//
// HAI ĐƯỜNG RA, DÙNG ĐƯỢC CÙNG LÚC
//   outputPath — ghi file .h264/.mp4, dùng để kiểm chứng bằng ffplay (di sản GĐ1,
//                vẫn giữ vì nó là cách gỡ lỗi nhanh nhất khi hình ra sai).
//   onPacket   — trả NAL cho tầng mạng. Đây là đường thật của streaming.
//   Để rỗng cái nào thì tắt đường đó.
//
// ⚠ QUY TẮC VÒNG ĐỜI CỦA onPacket
//   Callback chạy ĐỒNG BỘ trên chính thread gọi Encode(), và `data` chỉ hợp lệ
//   trong phạm vi lời gọi. Hai hệ quả:
//     - Phải copy hoặc tiêu thụ ngay, không giữ con trỏ lại.
//     - Không được làm việc chậm trong đó. Encode() thường được gọi từ callback
//       FrameArrived của WGC, nên ngủ ở đây là làm đứng cả đường bắt hình — xem
//       Pacer.h về đúng lỗi này và cái giá của nó.
//
// VÌ SAO CÓ CẢ width/height LẪN srcWidth/srcHeight
//   H.264 dùng NV12, lấy mẫu chroma theo khối 2×2, nên kích thước NÉN bắt buộc phải
//   CHẴN. Nhưng cửa sổ người dùng chọn có thể rộng hoặc cao lẻ. Khi đó ta nén ở
//   kích thước chẵn nhỏ hơn một điểm ảnh, còn texture đưa vào vẫn giữ kích thước
//   lẻ thật — video processor cần biết CẢ HAI để cắt cho đúng thay vì co giãn méo.
//   Bằng nhau thì để srcWidth/srcHeight = 0.
//
// LIÊN QUAN: encode/NvencEncoder.h, encode/MfEncoder.h (hai bản cài đặt),
//            encode/EncoderFactory.cpp (chọn cái nào), capture/CaptureTypes.h
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "deskhub/media/VideoContract.h"

// Từ vựng dùng chung với bốn nền kia — Codec, RateControl, PacketHandler và các
// trường của EncoderConfig nay định nghĩa MỘT lần ở deskhub/media/VideoTypes.h
// (trước 31/07/2026 chúng có ba bản, và ba bản đã lệch nhau).
using deskhub::media::Codec;
using deskhub::media::PacketHandler;
using deskhub::media::RateControl;

// Bản Windows THÊM đúng một trường: đường ghi file .h264/.mp4 để kiểm chứng bằng
// ffplay (di sản GĐ1, AgentLoop luôn xoá nó đi). Nó là std::wstring và chỉ Media
// Foundation/NVENC dùng, nên nó ở lại đây chứ không lên core.
struct EncoderConfig : deskhub::media::EncoderConfig {
    std::wstring outputPath = L"output.mp4"; // rỗng = không ghi file
};

class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;

    // Khởi tạo trên device ĐÃ CHỌN (chia sẻ với capture). Trả về false nếu backend không dùng được.
    virtual bool Init(ID3D11Device* device, const EncoderConfig& cfg) = 0;

    // Nén một frame VRAM. `timestampUs` từ capture. `forceKeyframe` xin IDR (dùng khi mất gói).
    virtual bool Encode(ID3D11Texture2D* frame, uint64_t timestampUs, bool forceKeyframe) = 0;

    // GD5: đổi bitrate mục tiêu giữa chừng (congestion control theo FEEDBACK).
    // Không dựng lại encoder — chuỗi inter-frame giữ nguyên, không cần IDR.
    // false = backend không đổi được, caller cứ chạy tiếp với bitrate cũ.
    virtual bool SetBitrate(uint32_t bitrateBps) = 0;

    // Đổi fps kỳ vọng giữa chừng (deskhub::QualityLadder hạ bậc).
    //
    // VÌ SAO PHẢI NÓI cho encoder biết khi ta chỉ đơn giản là NỘP ÍT FRAME HƠN:
    //   fps là MẪU SỐ bộ điều khiển tốc độ dùng để chia ngân sách bit cho từng frame,
    //   và cũng là mẫu số tính cỡ VBV. Nộp 20 frame/giây mà nó vẫn tưởng 60 thì mỗi
    //   frame chỉ được tiêu một phần ba số bit đáng ra được tiêu — ta hạ fps để hình
    //   NÉT HƠN mà nhận về hình MỜ HƠN, đúng ngược mục đích của cả cái thang.
    //
    // ⚠ CHI PHÍ KHÁC NHAU GIỮA HAI BACKEND, và caller phải biết:
    //   NVENC  — nvEncReconfigureEncoder, KHÔNG dựng lại session, không cần IDR.
    //   MF     — Media Foundation không có núm chỉnh fps khi đang chạy; fps nằm trong
    //            media type, nên đổi nó là dựng lại transform => frame kế tiếp LÀ MỘT
    //            IDR. Đó là lý do thang chỉ đổi bậc mỗi vài giây chứ không mỗi giây.
    virtual bool SetFps(uint32_t fps) = 0;

    // Flush + finalize (ghi xong file / đóng stream).
    virtual void Finish() = 0;

    // const char* chứ không phải const wchar_t*: cùng kiểu với bốn nền kia, để
    // deskhub::media::VideoEncoderLike ép được một chữ ký duy nhất. Tên backend
    // toàn ASCII nên không mất gì.
    virtual const char* BackendName() const = 0;
};

// Factory: thử các backend theo thứ tự, trả về cái đầu tiên Init thành công.
// Hiện tại: Media Foundation (tự chọn HW theo device: NVENC/QSV, hoặc software).
std::unique_ptr<IVideoEncoder> CreateEncoder(ID3D11Device* device, const EncoderConfig& cfg);

// Hợp đồng chữ ký, ép lúc BIÊN DỊCH (deskhub/media/VideoContract.h). Windows là nền
// DUY NHẤT còn giữ lớp cơ sở ảo thật — nó cần chọn NVENC hay Media Foundation lúc
// chạy. Bốn nền kia mỗi nền một backend nên chỉ có static_assert. Cả năm cùng bị
// ép bởi đúng một concept, nên chữ ký không thể trôi khỏi nhau nữa.
static_assert(deskhub::media::VideoEncoderLike<IVideoEncoder, ID3D11Texture2D*>,
    "IVideoEncoder phải giữ đúng chữ ký chung của bộ nén");
static_assert(deskhub::media::HotFpsEncoder<IVideoEncoder>,
    "IVideoEncoder chỉnh nóng được fps (NVENC reconfigure; MF dựng lại transform)");
