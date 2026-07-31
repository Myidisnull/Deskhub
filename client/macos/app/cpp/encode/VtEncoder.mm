#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <VideoToolbox/VideoToolbox.h>

#include "encode/VtEncoder.h"

#include <cstring>

#include "deskhubp/diag/Log.h"

namespace {

constexpr uint8_t kStartCode[4] = {0, 0, 0, 1};

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

void OutputCallback(void* refcon, void* , OSStatus status,
    VTEncodeInfoFlags infoFlags, CMSampleBufferRef sampleBuffer) {
    if (auto* self = static_cast<VtEncoder*>(refcon))
        self->OnEncoded((void*)sampleBuffer, int32_t(status), uint32_t(infoFlags));
}

}

void VtEncoder::OnEncoded(void* sampleBufferOpaque, int32_t status, uint32_t infoFlags) {
    auto* self = this;
    auto sb = static_cast<CMSampleBufferRef>(sampleBufferOpaque);
    if (!self->cfg_.onPacket) return;
    if (status != noErr) {
        LOGE("[Encoder] Compression failed: %d", int(status));
        return;
    }
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

    std::lock_guard<std::mutex> lk(self->emitMutex_);
    std::vector<uint8_t>& out = self->annexb_;
    out.clear();
    out.reserve(totalLen + 256);

    int nalLenSize = 4;
    CMFormatDescriptionRef fmt = CMSampleBufferGetFormatDescription(sb);
    if (fmt) {
        size_t psCount = 0;
        int lenSize = 4;
        if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(fmt, 0, nullptr, nullptr,
                &psCount, &lenSize) == noErr) {
            nalLenSize = lenSize;
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
    setNum(kVTCompressionPropertyKey_MaxKeyFrameInterval, INT32_MAX);
    setNum(kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, INT32_MAX);
    setNum(kVTCompressionPropertyKey_ExpectedFrameRate, int64_t(cfg.fps));
    setNum(kVTCompressionPropertyKey_MaxFrameDelayCount, 1);

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
    SetBitrate(cfg_.bitrateBps);
    return true;
}

bool VtEncoder::Encode(void* pixelBuffer, uint64_t timestampUs, bool forceKeyframe) {
    if (!session_ || !pixelBuffer) return false;
    auto session = static_cast<VTCompressionSessionRef>(session_);
    auto pb = static_cast<CVPixelBufferRef>(pixelBuffer);

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
    VTCompressionSessionCompleteFrames(static_cast<VTCompressionSessionRef>(session_),
        kCMTimeInvalid);
}

void VtEncoder::Finish() {
    if (!session_) return;
    auto session = static_cast<VTCompressionSessionRef>(session_);
    VTCompressionSessionCompleteFrames(session, kCMTimeInvalid);
    VTCompressionSessionInvalidate(session);
    CFRelease(session);
    session_ = nullptr;
}
