#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <VideoToolbox/VideoToolbox.h>

#include "decode/VtDecoder.h"

#include <cstring>
#include <vector>

#include "deskhubp/Log.h"

namespace {

struct Nal {
    const uint8_t* ptr;
    size_t len;
    uint8_t type;
};

std::vector<Nal> ParseAnnexB(const uint8_t* d, size_t n) {
    std::vector<Nal> out;
    auto sc4 = [&](size_t p) {
        return p + 3 < n && d[p] == 0 && d[p + 1] == 0 && d[p + 2] == 0 && d[p + 3] == 1;
    };
    auto sc3 = [&](size_t p) {
        return p + 2 < n && d[p] == 0 && d[p + 1] == 0 && d[p + 2] == 1;
    };

    size_t p = 0;
    while (p < n) {
        size_t sc = sc4(p) ? 4 : (sc3(p) ? 3 : 0);
        if (sc == 0) {
            ++p;
            continue;
        }
        const size_t start = p + sc;
        size_t q = start;
        while (q < n && !sc4(q) && !sc3(q)) ++q;
        if (start < q)
            out.push_back(Nal{d + start, q - start, uint8_t(d[start] & 0x1F)});
        p = q;
    }
    return out;
}

void AppendAvcc(std::vector<uint8_t>& out, const uint8_t* nal, size_t len) {
    out.push_back(uint8_t(len >> 24));
    out.push_back(uint8_t(len >> 16));
    out.push_back(uint8_t(len >> 8));
    out.push_back(uint8_t(len));
    out.insert(out.end(), nal, nal + len);
}

}

VtDecoder::~VtDecoder() {
    Shutdown();
}

bool VtDecoder::Init(void* layer, int width, int height) {
    Shutdown();
    if (!layer) return false;
    layer_ = layer;
    rendered_ = 0;
    lastRenderedPtsUs_ = 0;
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
        [l flush];
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

    if (l.status == AVQueuedSampleBufferRenderingStatusFailed) {
        LOGW("[Decoder] display layer failed (%s); flushing.",
            l.error ? l.error.localizedDescription.UTF8String : "unknown");
        [l flush];
        CFRelease(sb);
        return false;
    }

    if (!l.isReadyForMoreMediaData) {
        ++congestionDrops_;
        CFRelease(sb);
        return true;
    }

    [l enqueueSampleBuffer:sb];
    CFRelease(sb);

    ++rendered_;
    lastRenderedPtsUs_ = ptsUs;
    return true;
}

uint32_t VtDecoder::TakeRenderedCount() {
    const uint32_t n = rendered_;
    rendered_ = 0;
    return n;
}
