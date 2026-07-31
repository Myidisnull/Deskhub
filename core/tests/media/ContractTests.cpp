// =============================================================================
// ContractTests.cpp — test cho deskhub/media/VideoContract.h.
//
// VÌ SAO FILE NÀY TỒN TẠI
//   static_assert nằm trong client/*/encode|decode chỉ chạy khi nền đó được build,
//   mà không máy nào build được cả năm nền: Windows cần MSVC + Media Foundation,
//   Apple cần Xcode, Android cần NDK. Nghĩa là hợp đồng — thứ sinh ra để chống
//   trôi — lại chỉ được kiểm ở đúng nền người ta đang ngồi.
//
//   Ở đây ta dựng lớp GIẢ mang ĐÚNG chữ ký của từng nền (chép từ header thật) rồi
//   bắt concept phán xử chúng. Nhờ vậy core_tests — chạy được bằng mọi toolchain —
//   kiểm được hợp đồng cho cả năm nền cùng lúc, kể cả nền không có ở đây.
//
//   Kèm theo là các ca PHẢN — chữ ký sai một chút — để chứng minh concept thật sự
//   TỪ CHỐI chứ không phải nhận bừa. Một concept luôn đúng thì vô dụng, và đó là
//   kiểu hỏng mà không ai phát hiện ra.
//
// TOÀN BỘ FILE NÀY LÀ TEST LÚC BIÊN DỊCH. Hàm Run ở cuối gần như rỗng — nếu file
// này dịch được thì mọi thứ nó khẳng định đã đúng.
//
// LIÊN QUAN: deskhub/media/VideoContract.h, deskhub/media/VideoTypes.h
// =============================================================================
#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/media/VideoContract.h"

#include <cstdio>

using namespace deskhub::media;

namespace {

// Handle frame giả của từng nền — chỉ cần là một kiểu, nội dung không quan trọng.
struct D3D11Texture; // Windows: ID3D11Texture2D*
struct LinuxFrame;   // Ubuntu: const LinuxFrameInfo&

// --- Bộ nén: bốn hình dạng có thật trong dự án -------------------------------

// Windows (IVideoEncoder): ảo, chỉnh nóng được fps.
struct WinEncoderShape {
    virtual ~WinEncoderShape() = default;
    virtual bool Encode(D3D11Texture* frame, uint64_t timestampUs, bool forceKeyframe) = 0;
    virtual bool SetBitrate(uint32_t) = 0;
    virtual bool SetFps(uint32_t) = 0;
    virtual void Finish() = 0;
    virtual const char* BackendName() const = 0;
};

// macOS (VtEncoder): frame là CVPixelBufferRef truyền qua void*.
struct MacEncoderShape {
    bool Encode(void* pixelBuffer, uint64_t timestampUs, bool forceKeyframe);
    bool SetBitrate(uint32_t);
    bool SetFps(uint32_t);
    void Finish();
    const char* BackendName() const;
};

// Ubuntu (VaEncoder): frame là struct giàu thông tin truyền theo tham chiếu const,
// và KHÔNG có SetFps — VA-API không chỉnh nóng được, AgentLoop dựng lại encoder.
struct LinuxEncoderShape {
    bool Encode(const LinuxFrame& fi, uint64_t timestampUs, bool forceKeyframe);
    bool SetBitrate(uint32_t);
    void Finish();
    const char* BackendName() const;
};

// --- Bộ giải nén -------------------------------------------------------------

// Apple + Android: dựng lại tại chỗ, tự đếm frame đã lên màn, báo được nghẽn hiển thị.
struct AppleDecoderShape {
    bool Decode(const uint8_t* nal, size_t len, uint64_t ptsUs);
    void Shutdown();
    bool IsOpen() const;
    uint32_t TakeRenderedCount();
    uint64_t lastRenderedPtsUs() const;
    uint32_t TakeCongestionDrops();
};

// Ubuntu (AvDecoder): dựng lại tại chỗ, nhưng số frame vẽ đi qua VideoSink chứ
// không qua decoder, và không có khái niệm nghẽn tầng hiển thị.
struct LinuxDecoderShape {
    bool Decode(const uint8_t* nal, size_t len, uint64_t ptsUs);
    void Shutdown();
    bool IsOpen() const;
};

// Windows (IVideoDecoder): dựng lại bằng cách tạo đối tượng mới, nên không có
// Shutdown/IsOpen.
struct WinDecoderShape {
    virtual ~WinDecoderShape() = default;
    virtual bool Decode(const uint8_t* data, size_t size, uint64_t timestampUs) = 0;
    virtual const char* BackendName() const = 0;
};

// --- Ca PHẢN: phải bị TỪ CHỐI ------------------------------------------------

struct WrongReturnType { // SetBitrate trả void thay vì bool -> mất đường báo "backend từ chối"
    bool Encode(void*, uint64_t, bool);
    void SetBitrate(uint32_t);
    void Finish();
    const char* BackendName() const;
};

struct WrongArgOrder { // đảo timestamp và cờ keyframe
    bool Encode(void*, bool, uint64_t);
    bool SetBitrate(uint32_t);
    void Finish();
    const char* BackendName() const;
};

struct WideBackendName { // const wchar_t* — đúng cái lệch của Windows trước 31/07/2026
    bool Encode(void*, uint64_t, bool);
    bool SetBitrate(uint32_t);
    void Finish();
    const wchar_t* BackendName() const;
};

struct MissingFinish {
    bool Encode(void*, uint64_t, bool);
    bool SetBitrate(uint32_t);
    const char* BackendName() const;
};

struct DecoderTakingIntPts { // ptsUs là int -> mốc thời gian tràn sau ~35 phút
    bool Decode(const uint8_t*, size_t, int);
};

// --- Phán xử ------------------------------------------------------------------

// Năm nền, một hợp đồng.
static_assert(VideoEncoderLike<WinEncoderShape, D3D11Texture*>);
static_assert(VideoEncoderLike<MacEncoderShape, void*>);
static_assert(VideoEncoderLike<LinuxEncoderShape, const LinuxFrame&>);

// Khả năng TÙY CHỌN phải phân biệt được đúng nền có và nền không.
static_assert(HotFpsEncoder<WinEncoderShape>);
static_assert(HotFpsEncoder<MacEncoderShape>);
static_assert(!HotFpsEncoder<LinuxEncoderShape>, "VA-API không chỉnh nóng được fps");

static_assert(VideoDecoderLike<AppleDecoderShape>);
static_assert(VideoDecoderLike<LinuxDecoderShape>);
static_assert(VideoDecoderLike<WinDecoderShape>);

static_assert(RestartableDecoder<AppleDecoderShape>);
static_assert(RestartableDecoder<LinuxDecoderShape>);
static_assert(!RestartableDecoder<WinDecoderShape>, "Windows dựng lại bằng đối tượng mới");

static_assert(RenderCountingDecoder<AppleDecoderShape>);
static_assert(!RenderCountingDecoder<LinuxDecoderShape>, "Ubuntu đếm ở VideoSink");

static_assert(CongestionAwareDecoder<AppleDecoderShape>);
static_assert(!CongestionAwareDecoder<LinuxDecoderShape>,
    "chỉ nền có tầng hiển thị bất đồng bộ mới có disp_drop");

// Concept phải BIẾT TỪ CHỐI — nếu bốn dòng này không còn đúng thì hợp đồng đã hỏng
// theo kiểu tệ nhất: nó nhận mọi thứ và không bảo vệ được gì.
static_assert(!VideoEncoderLike<WrongReturnType, void*>);
static_assert(!VideoEncoderLike<WrongArgOrder, void*>);
static_assert(!VideoEncoderLike<WideBackendName, void*>);
static_assert(!VideoEncoderLike<MissingFinish, void*>);
static_assert(!VideoDecoderLike<DecoderTakingIntPts>);

} // namespace

void RunMediaContractTests() {
    std::printf("[media] hợp đồng chữ ký encoder/decoder cho cả năm nền (kiểm lúc biên dịch)...\n");
    // Phần trên file này là test LÚC BIÊN DỊCH — dịch được nghĩa là đã đúng.

    // Còn đây là phần chạy được: giá trị MẶC ĐỊNH của từ vựng dùng chung. Chúng là
    // hợp đồng với người đọc log chứ không phải chi tiết cài đặt — đổi một trong
    // số này là đổi hành vi mặc định của cả năm nền cùng lúc.
    //
    // Không dùng static_assert được: EncoderConfig chứa std::function (onPacket)
    // nên nó không phải kiểu literal, không dựng được trong biểu thức hằng.
    std::printf("[media] giá trị mặc định của EncoderConfig/DecoderConfig...\n");
    const EncoderConfig ec;
    Check(ec.fps == 60, "EncoderConfig: fps mặc định 60");
    Check(ec.codec == Codec::H264, "EncoderConfig: codec mặc định H264");
    Check(ec.rc == RateControl::CBR, "EncoderConfig: rate control mặc định CBR");
    Check(ec.lowLatency, "EncoderConfig: mặc định ưu tiên độ trễ thấp");
    Check(ec.bitrateBps == 20'000'000, "EncoderConfig: bitrate mặc định 20 Mbps");
    Check(ec.srcWidth == 0 && ec.srcHeight == 0,
        "EncoderConfig: 0 nghĩa là 'bằng width/height' — nền nào cũng hiểu như vậy");
    Check(!ec.onPacket, "EncoderConfig: chưa nối onPacket thì không có đường ra nào");

    const DecoderConfig dc;
    Check(dc.fps == 60, "DecoderConfig: fps mặc định 60");
    Check(dc.codec == Codec::H264, "DecoderConfig: codec mặc định H264");
    Check(dc.width == 0 && dc.height == 0,
        "DecoderConfig: 0 = chưa biết, decoder tự đọc lại từ SPS");
}
