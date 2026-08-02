#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <VideoToolbox/VideoToolbox.h>

#include "deskhubp/media/VtDecoder.h"

#include <cstring>
#include <span>
#include <vector>

#include "deskhub/media/AnnexB.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/system/Clock.h"

namespace {

struct Nal {
    const uint8_t* ptr;
    size_t len;
    uint8_t type;
};

std::vector<Nal> ParseAnnexB(const uint8_t* d, size_t n) {
    std::vector<Nal> out;
    for (const deskhub::media::NalRef& x :
        deskhub::media::ParseAnnexB(std::span<const uint8_t>(d, n)))
        out.push_back(Nal{d + x.offset, x.size, deskhub::media::H264NalType(x.header)});
    return out;
}

void AppendAvcc(std::vector<uint8_t>& out, const uint8_t* nal, size_t len) {
    deskhub::media::AppendLengthPrefixed(out, std::span<const uint8_t>(nal, len));
}

}

VtDecoder::~VtDecoder() {
    Shutdown();
}

bool VtDecoder::Init(void* layer, int width, int height) {
    Shutdown();
    if (!layer) return false;
    layer_ = layer;
    counters_.Reset();
    LOGI("[Decoder] VideoToolbox H.264 target %dx%d ready (AVSampleBufferDisplayLayer).",
        width, height);
    return true;
}

void VtDecoder::Shutdown() {
    if (formatDesc_) {
        CFRelease((CMFormatDescriptionRef)formatDesc_);
        formatDesc_ = nullptr;
    }
    if (layer_) {
        AVSampleBufferDisplayLayer* l = (__bridge AVSampleBufferDisplayLayer*)layer_;
        [l.sampleBufferRenderer flush];
        layer_ = nullptr;
    }
    spsLen_ = ppsLen_ = 0;
}

bool VtDecoder::Decode(const uint8_t* nal, size_t len, uint64_t ptsUs) {
    if (!layer_ || !nal || len == 0) return false;

    const std::vector<Nal> nals = ParseAnnexB(nal, len);

    const Nal* sps = nullptr;
    const Nal* pps = nullptr;
    for (const Nal& x : nals) {
        if (x.type == 7)
            sps = &x;
        else if (x.type == 8)
            pps = &x;
    }
    if (sps && pps && (sps->len > sizeof(sps_) || pps->len > sizeof(pps_))) {
        LOGE("[Decoder] SPS/PPS too large (%zu/%zu bytes) — rejecting frame.",
            sps->len, pps->len);
        return false;
    }
    if (sps && pps) {
        const bool changed = !formatDesc_ || sps->len != spsLen_ || pps->len != ppsLen_ ||
                             std::memcmp(sps->ptr, sps_, spsLen_) != 0 ||
                             std::memcmp(pps->ptr, pps_, ppsLen_) != 0;
        if (changed) {
            if (formatDesc_) {
                CFRelease((CMFormatDescriptionRef)formatDesc_);
                formatDesc_ = nullptr;
            }
            const uint8_t* params[2] = {sps->ptr, pps->ptr};
            const size_t sizes[2] = {sps->len, pps->len};
            CMFormatDescriptionRef fmt = nullptr;
            const OSStatus st = CMVideoFormatDescriptionCreateFromH264ParameterSets(
                kCFAllocatorDefault, 2, params, sizes, 4, &fmt);
            if (st != noErr || !fmt) {
                LOGE("[Decoder] CMVideoFormatDescription from SPS/PPS failed: %d", int(st));
                return false;
            }
            formatDesc_ = (void*)fmt;
            std::memcpy(sps_, sps->ptr, sps->len);
            spsLen_ = sps->len;
            std::memcpy(pps_, pps->ptr, pps->len);
            ppsLen_ = pps->len;
        }
    }

    if (!formatDesc_) return true;

    avcc_.clear();
    avcc_.reserve(len);
    for (const Nal& x : nals) {
        if (x.type == 7 || x.type == 8 || x.type == 9) continue;
        AppendAvcc(avcc_, x.ptr, x.len);
    }
    if (avcc_.empty()) return true;

    CMFormatDescriptionRef fmt = (CMFormatDescriptionRef)formatDesc_;

    CMBlockBufferRef bb = nullptr;
    OSStatus st = CMBlockBufferCreateWithMemoryBlock(
        kCFAllocatorDefault, nullptr, avcc_.size(), kCFAllocatorDefault, nullptr,
        0, avcc_.size(), kCMBlockBufferAssureMemoryNowFlag, &bb);
    if (st != noErr || !bb) {
        LOGE("[Decoder] CMBlockBufferCreate failed: %d", int(st));
        return false;
    }
    st = CMBlockBufferReplaceDataBytes(avcc_.data(), bb, 0, avcc_.size());
    if (st != noErr) {
        CFRelease(bb);
        return false;
    }

    CMSampleTimingInfo timing;
    timing.duration = kCMTimeInvalid;
    timing.presentationTimeStamp = CMTimeMake(int64_t(ptsUs), 1'000'000);
    timing.decodeTimeStamp = kCMTimeInvalid;
    const size_t sampleSize = avcc_.size();

    CMSampleBufferRef sb = nullptr;
    st = CMSampleBufferCreateReady(kCFAllocatorDefault, bb, fmt, 1, 1, &timing,
        1, &sampleSize, &sb);
    CFRelease(bb);
    if (st != noErr || !sb) {
        LOGE("[Decoder] CMSampleBufferCreateReady failed: %d", int(st));
        return false;
    }

    if (CFArrayRef atts = CMSampleBufferGetSampleAttachmentsArray(sb, true)) {
        if (CFArrayGetCount(atts) > 0) {
            CFMutableDictionaryRef d0 =
                (CFMutableDictionaryRef)CFArrayGetValueAtIndex(atts, 0);
            CFDictionarySetValue(d0, kCMSampleAttachmentKey_DisplayImmediately,
                kCFBooleanTrue);
        }
    }

    AVSampleBufferDisplayLayer* l = (__bridge AVSampleBufferDisplayLayer*)layer_;
    AVSampleBufferVideoRenderer* r = l.sampleBufferRenderer;

    if (r.status == AVQueuedSampleBufferRenderingStatusFailed) {
        LOGW("[Decoder] display layer failed (%s); flushing.",
            r.error ? r.error.localizedDescription.UTF8String : "unknown");
        [r flush];
        CFRelease(sb);
        return false;
    }

    if (!r.isReadyForMoreMediaData) {
        counters_.CountCongestionDrop();
        CFRelease(sb);
        return true;
    }

    [r enqueueSampleBuffer:sb];
    CFRelease(sb);

    counters_.FramePresented(ptsUs, NowUs());
    return true;
}
