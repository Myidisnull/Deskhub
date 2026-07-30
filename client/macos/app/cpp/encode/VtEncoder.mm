// =============================================================================
// VtEncoder.mm — cài đặt bằng VTCompressionSession.
//
// BỐ CỤC
//   OutputCallback  — VideoToolbox gọi về khi có frame nén xong; chỉ chuyển tiếp.
//   OnEncoded()     — AVCC → Annex-B (+ SPS/PPS nếu IDR) rồi giao onPacket.
//   Init/Encode/SetBitrate/Finish — vòng đời, khớp API của IVideoEncoder bên Windows.
//
// TỪNG NÚM LOW-LATENCY VÀ VÌ SAO CẦN NÓ
//   RealTime=true          — bảo encoder ưu tiên trả frame ĐÚNG HẠN hơn là nén đẹp.
//                            Thiếu nó, VideoToolbox gom frame lại nén theo lô và độ
//                            trễ vọt lên hàng trăm ms.
//   AllowFrameReordering=false — tắt B-frame. B-frame cần frame TƯƠNG LAI mới giải
//                            được, tức là cộng thẳng một khoảng chờ vào độ trễ. Nó
//                            cũng là điều kiện để VtDecoder đặt DisplayImmediately.
//   MaxKeyFrameInterval=INT_MAX — GOP VÔ HẠN: không phát IDR định kỳ (IDR to gấp
//                            hàng chục lần P-frame, phát đều đặn là đốt bitrate vô
//                            ích), chỉ phát khi client xin. Đúng chính sách docs/02 §3.
//   DataRateLimits         — trần burst. AverageBitRate một mình chỉ ràng buộc trung
//                            bình dài hạn, nên một cảnh đổi đột ngột có thể bắn ra
//                            một cụm gói làm tràn buffer mạng — đúng loại mất gói
//                            mà FEC không cứu được.
//
// VÌ SAO CHẤP NHẬN CẢ BỘ MÃ HOÁ PHẦN MỀM
//   EnableHardwareAcceleratedVideoEncoder là YÊU CẦU chứ không phải bắt buộc: máy
//   Intel đời cũ hoặc phiên chạy dưới màn hình ảo có thể không có bộ mã hoá phần
//   cứng. Thà nén bằng CPU (tốn điện, vẫn chạy) còn hơn từ chối chia sẻ. Ta chỉ ghi
//   lại sự thật đó vào BackendName() để chẩn đoán khi người dùng than nóng máy.
//
// LIÊN QUAN: encode/VtEncoder.h (cảnh báo AVCC→Annex-B + vòng đời onPacket)
// =============================================================================
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <VideoToolbox/VideoToolbox.h>

#include "encode/VtEncoder.h"

#include <cstring>

#include "Log.h"

namespace {

constexpr uint8_t kStartCode[4] = {0, 0, 0, 1};

// Frame này có phải IDR không. VideoToolbox không nói "đây là IDR" mà nói ngược lại:
// gắn NotSync=true cho frame KHÔNG phải điểm đồng bộ. Không có attachment nào thì
// mặc định là sync frame.
bool IsKeyframe(CMSampleBufferRef sb) {
    CFArrayRef atts = CMSampleBufferGetSampleAttachmentsArray(sb, false);
    if (!atts || CFArrayGetCount(atts) == 0) return true;
    CFDictionaryRef d = (CFDictionaryRef)CFArrayGetValueAtIndex(atts, 0);
    CFBooleanRef notSync = nullptr;
    if (!CFDictionaryGetValueIfPresent(d, kCMSampleAttachmentKey_NotSync,
            (const void**)&notSync))
        return true;
    return !(notSync && CFBooleanGetValue(notSync));
}

void AppendNal(std::vector<uint8_t>& out, const uint8_t* nal, size_t len) {
    out.insert(out.end(), kStartCode, kStartCode + 4);
    out.insert(out.end(), nal, nal + len);
}

// VideoToolbox gọi hàm này trên thread nội bộ của nó. `refcon` là VtEncoder*.
// Chữ ký phải khớp TỪNG KIỂU với VTCompressionOutputCallback — con trỏ hàm trong
// C++ không có chuyển đổi ngầm, sai một kiểu là lỗi biên dịch ở lời gọi Create.
void OutputCallback(void* refcon, void* /*sourceFrameRefcon*/, OSStatus status,
    VTEncodeInfoFlags infoFlags, CMSampleBufferRef sampleBuffer) {
    if (auto* self = static_cast<VtEncoder*>(refcon))
        self->OnEncoded((void*)sampleBuffer, int32_t(status), uint32_t(infoFlags));
}

} // namespace

void VtEncoder::OnEncoded(void* sampleBufferOpaque, int32_t status, uint32_t infoFlags) {
    auto* self = this;
    auto sb = static_cast<CMSampleBufferRef>(sampleBufferOpaque);
    if (!self->cfg_.onPacket) return;
    if (status != noErr) {
        LOGE("[Encoder] Compression failed: %d", int(status));
        return;
    }
    // kVTEncodeInfo_FrameDropped: encoder bỏ frame vì quá tải. Không phải lỗi — bỏ
    // qua im lặng, frame sau sẽ tới. Thống kê fps ở AgentLoop tự lộ ra nếu bỏ nhiều.
    if (infoFlags & kVTEncodeInfo_FrameDropped) return;
    if (!sb || !CMSampleBufferDataIsReady(sb)) return;

    CMBlockBufferRef bb = CMSampleBufferGetDataBuffer(sb);
    if (!bb) return;

    size_t totalLen = 0;
    char* dataPtr = nullptr;
    if (CMBlockBufferGetDataPointer(bb, 0, nullptr, &totalLen, &dataPtr) != kCMBlockBufferNoErr ||
        !dataPtr || !totalLen)
        return;

    const bool keyframe = IsKeyframe(sb);

    // Khoá suốt từ đây: nó vừa nối tiếp hoá onPacket (Packetizer đòi single-thread)
    // vừa bảo vệ bộ đệm annexb_ dùng lại.
    std::lock_guard<std::mutex> lk(self->emitMutex_);
    std::vector<uint8_t>& out = self->annexb_;
    out.clear();
    out.reserve(totalLen + 256); // + chỗ cho SPS/PPS khi là IDR

    // Số byte của tiền tố độ dài AVCC. Gần như luôn là 4, nhưng đọc ra chứ không giả
    // định: chuẩn cho phép 1/2/4 và đọc sai thì cả frame thành rác.
    int nalLenSize = 4;
    CMFormatDescriptionRef fmt = CMSampleBufferGetFormatDescription(sb);
    if (fmt) {
        size_t psCount = 0;
        int lenSize = 4;
        if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(fmt, 0, nullptr, nullptr,
                &psCount, &lenSize) == noErr) {
            nalLenSize = lenSize;
            // IDR mang SPS/PPS in-band — xem cảnh báo ở VtEncoder.h. Client vào phiên
            // giữa chừng chỉ nhận được tham số qua đường này.
            if (keyframe) {
                for (size_t i = 0; i < psCount; ++i) {
                    const uint8_t* ps = nullptr;
                    size_t psSize = 0;
                    if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(fmt, i, &ps, &psSize,
                            nullptr, nullptr) == noErr &&
                        ps && psSize)
                        AppendNal(out, ps, psSize);
                }
            }
        }
    }

    // Duyệt chuỗi AVCC: [len][nal][len][nal]... Kiểm tra biên ở mỗi bước — một
    // trường độ dài hỏng sẽ đẩy con trỏ ra ngoài vùng nhớ.
    const auto* p = reinterpret_cast<const uint8_t*>(dataPtr);
    size_t off = 0;
    while (off + size_t(nalLenSize) <= totalLen) {
        size_t nalLen = 0;
        for (int i = 0; i < nalLenSize; ++i) nalLen = (nalLen << 8) | p[off + size_t(i)];
        off += size_t(nalLenSize);
        if (!nalLen || off + nalLen > totalLen) break;
        AppendNal(out, p + off, nalLen);
        off += nalLen;
    }
    if (out.empty()) return;

    // PTS ta tự đặt lúc Encode (đồng hồ NowUs của dự án, timescale 1e6) quay về
    // nguyên vẹn ở đây — đó là mốc client dùng để tính e2e.
    const CMTime pts = CMSampleBufferGetPresentationTimeStamp(sb);
    const uint64_t tsUs = CMTIME_IS_VALID(pts) ? uint64_t(pts.value) : 0;

    self->cfg_.onPacket(out.data(), out.size(), tsUs, keyframe);
}

VtEncoder::~VtEncoder() {
    Finish();
}

bool VtEncoder::Init(const EncoderConfig& cfg) {
    Finish();
    if (!cfg.width || !cfg.height || (cfg.width & 1) || (cfg.height & 1)) {
        LOGE("[Encoder] Bad size %ux%u (must be non-zero and even).", cfg.width, cfg.height);
        return false;
    }
    cfg_ = cfg;

    NSDictionary* spec = @{
        (__bridge NSString*)kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder : @YES,
    };
    // Nói trước cho VideoToolbox biết buffer vào là NV12 và IOSurface-backed: nhờ
    // vậy nó nhận thẳng buffer của SCStream, không phải chép qua một pool trung gian.
    NSDictionary* srcAttrs = @{
        (__bridge NSString*)kCVPixelBufferPixelFormatTypeKey :
            @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
        (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey : @{},
    };

    VTCompressionSessionRef session = nullptr;
    OSStatus st = VTCompressionSessionCreate(kCFAllocatorDefault,
        int32_t(cfg.width), int32_t(cfg.height), kCMVideoCodecType_H264,
        (__bridge CFDictionaryRef)spec, (__bridge CFDictionaryRef)srcAttrs,
        kCFAllocatorDefault, OutputCallback, this, &session);
    if (st != noErr || !session) {
        LOGE("[Encoder] VTCompressionSessionCreate failed: %d", int(st));
        return false;
    }
    session_ = session;

    // Bộ mã hoá thật sự dùng là phần cứng hay phần mềm? Chỉ để chẩn đoán.
    {
        CFBooleanRef hw = nullptr;
        if (VTSessionCopyProperty(session,
                kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder,
                kCFAllocatorDefault, &hw) == noErr &&
            hw) {
            hardware_ = CFBooleanGetValue(hw);
            CFRelease(hw);
        }
    }

    auto setNum = [session](CFStringRef key, int64_t v) {
        CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &v);
        VTSessionSetProperty(session, key, n);
        CFRelease(n);
    };

    VTSessionSetProperty(session, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
    VTSessionSetProperty(session, kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);
    VTSessionSetProperty(session, kVTCompressionPropertyKey_ProfileLevel,
        kVTProfileLevel_H264_High_AutoLevel);
    VTSessionSetProperty(session, kVTCompressionPropertyKey_H264EntropyMode,
        kVTH264EntropyMode_CABAC);
    // GOP vô hạn: IDR chỉ phát khi được xin. Đặt cả hai núm — MaxKeyFrameInterval
    // đếm theo SỐ FRAME, MaxKeyFrameIntervalDuration đếm theo GIÂY, và
    // VideoToolbox lấy cái nào tới trước. Đặt một cái thôi thì cái kia vẫn ép IDR.
    setNum(kVTCompressionPropertyKey_MaxKeyFrameInterval, INT32_MAX);
    setNum(kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, INT32_MAX);
    setNum(kVTCompressionPropertyKey_ExpectedFrameRate, int64_t(cfg.fps));

    if (!SetBitrate(cfg.bitrateBps)) {
        LOGW("[Encoder] Could not set initial bitrate %u bps.", cfg.bitrateBps);
    }

    VTCompressionSessionPrepareToEncodeFrames(session);
    LOGI("[Encoder] %s H.264 %ux%u @%ufps, %.1f Mbps.", BackendName(),
        cfg.width, cfg.height, cfg.fps, cfg.bitrateBps / 1e6);
    return true;
}

bool VtEncoder::SetBitrate(uint32_t bitrateBps) {
    if (!session_ || !bitrateBps) return false;
    auto session = static_cast<VTCompressionSessionRef>(session_);

    const int64_t bps = int64_t(bitrateBps);
    CFNumberRef avg = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &bps);
    const OSStatus st1 =
        VTSessionSetProperty(session, kVTCompressionPropertyKey_AverageBitRate, avg);
    CFRelease(avg);

    // DataRateLimits là mảng các CẶP [số byte, số giây]: "không quá N byte trong mỗi
    // cửa sổ T giây". Đặt HAI cặp, và cặp thứ hai mới là cặp quan trọng:
    //
    //   1. TRUNG BÌNH DÀI HẠN — 1.5× bitrate trong một giây. Đủ rộng để cảnh đổi đột
    //      ngột không bị nghẹt.
    //
    //   2. TRẦN BURST NGẮN HẠN — ⚠ THIẾU CẶP NÀY LÀ LỖI ĐÃ ĐO ĐƯỢC 30/07/2026.
    //      Một cửa sổ 1 giây KHÔNG ràng buộc được kích thước của MỘT frame: log
    //      client hôm đó cho thấy IDR ~205 KB, gấp 5 lần ngân sách một frame
    //      (20 Mbps / 60fps = 42 KB), bắn ra thành ~171 datagram liên tiếp. Client
    //      không mất gói (0.0%) nhưng hàng đợi giải mã tràn -> nó xin IDR -> host
    //      lại bắn 205 KB nữa: một vòng xoáy tự nuôi, 8 lần trong vài giây.
    //      Cả hai encoder bên Windows đã chặn đúng chỗ này từ 21/07 (MfEncoder:
    //      CODECAPI_AVEncCommonBufferSize = 2 frame; NvencEncoder: vbvBufferSize =
    //      1 frame) — VideoToolbox bị bỏ sót.
    //      Cho 2 frame trong 2 nhịp frame: IDR còn chỗ thở (nó vốn nặng hơn P-frame
    //      thật), nhưng không còn cửa để phình gấp năm.
    const uint32_t fps = cfg_.fps ? cfg_.fps : 60;
    const int64_t bytesPerSec = int64_t(double(bitrateBps) * 1.5 / 8.0);
    const double oneSecond = 1.0;
    const int64_t burstBytes = int64_t(double(bitrateBps) / 8.0 / fps * 2.0);
    const double burstSecs = 2.0 / double(fps);
    CFNumberRef b = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &bytesPerSec);
    CFNumberRef s = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &oneSecond);
    CFNumberRef b2 = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &burstBytes);
    CFNumberRef s2 = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &burstSecs);
    const void* vals[4] = {b, s, b2, s2};
    CFArrayRef limits = CFArrayCreate(kCFAllocatorDefault, vals, 4, &kCFTypeArrayCallBacks);
    VTSessionSetProperty(session, kVTCompressionPropertyKey_DataRateLimits, limits);
    CFRelease(limits);
    CFRelease(b);
    CFRelease(s);
    CFRelease(b2);
    CFRelease(s2);

    if (st1 == noErr) cfg_.bitrateBps = bitrateBps;
    return st1 == noErr;
}

bool VtEncoder::SetFps(uint32_t fps) {
    if (!session_ || !fps) return false;
    auto session = static_cast<VTCompressionSessionRef>(session_);
    const int64_t v = int64_t(fps);
    CFNumberRef n = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &v);
    const OSStatus st =
        VTSessionSetProperty(session, kVTCompressionPropertyKey_ExpectedFrameRate, n);
    CFRelease(n);
    if (st != noErr) return false;
    cfg_.fps = fps;
    // Trần burst tính theo fps (ngân sách một frame = bitrate/fps), nên đổi fps phải
    // đặt lại DataRateLimits — không thì hạ xuống 20fps vẫn giữ trần burst của 60fps
    // và cửa cho IDR phình lại mở ra.
    SetBitrate(cfg_.bitrateBps);
    return true;
}

bool VtEncoder::Encode(void* pixelBuffer, uint64_t timestampUs, bool forceKeyframe) {
    if (!session_ || !pixelBuffer) return false;
    auto session = static_cast<VTCompressionSessionRef>(session_);
    auto pb = static_cast<CVPixelBufferRef>(pixelBuffer);

    // timescale 1e6: PTS mang thẳng NowUs() của ta, nên callback đọc ra là con số
    // client cần để tính e2e — không có phép đổi đơn vị nào ở giữa để làm sai.
    const CMTime pts = CMTimeMake(int64_t(timestampUs), 1'000'000);

    NSDictionary* frameProps = nil;
    if (forceKeyframe)
        frameProps = @{(__bridge NSString*)kVTEncodeFrameOptionKey_ForceKeyFrame : @YES};

    VTEncodeInfoFlags flags = 0;
    const OSStatus st = VTCompressionSessionEncodeFrame(session, pb, pts, kCMTimeInvalid,
        (__bridge CFDictionaryRef)frameProps, nullptr, &flags);
    if (st != noErr) {
        LOGE("[Encoder] EncodeFrame failed: %d", int(st));
        return false;
    }
    return true;
}

void VtEncoder::Flush() {
    if (!session_) return;
    // kCMTimeInvalid = "hoàn tất MỌI frame", không phải tới một mốc nào đó. Callback
    // OnEncoded chạy trước khi hàm này trả về; nó chỉ lấy emitMutex_ nên không có
    // nguy cơ kẹt với encMutex mà người gọi đang giữ.
    VTCompressionSessionCompleteFrames(static_cast<VTCompressionSessionRef>(session_),
        kCMTimeInvalid);
}

void VtEncoder::Finish() {
    if (!session_) return;
    auto session = static_cast<VTCompressionSessionRef>(session_);
    // CompleteFrames trước Invalidate: nó đẩy nốt frame còn trong session ra callback
    // và CHỜ xong. Invalidate thẳng thì frame cuối biến mất — vô hại với video trực
    // tiếp, nhưng cũng có nghĩa callback có thể chạy sau khi ta đã xoá session_.
    VTCompressionSessionCompleteFrames(session, kCMTimeInvalid);
    VTCompressionSessionInvalidate(session);
    CFRelease(session);
    session_ = nullptr;
}
