// =============================================================================
// VtDecoder.mm — cài đặt giải mã + hiển thị bằng VideoToolbox / AVSampleBufferDisplayLayer.
//
// BỐ CỤC
//   ParseAnnexB()    — cắt một frame Annex-B thành danh sách NAL (con trỏ + kiểu).
//   RebuildFormat()  — dựng CMVideoFormatDescription từ SPS/PPS khi chúng đổi.
//   Decode()         — chuyển Annex-B→AVCC, đóng CMSampleBuffer, enqueue vào layer.
//
// QUY ƯỚC XỬ LÝ LỖI (khớp MediaCodecDecoder)
//   Decode() trả false nghĩa là "hỏng, dựng lại đi" (layer failed / dựng buffer
//   lỗi). Trả true mà không hiển thị gì là bình thường: frame trước IDR đầu tiên
//   chưa có SPS/PPS nên bị bỏ — Reassembler vốn đã chờ IDR.
//
// LIÊN QUAN: VtDecoder.h (mô hình + lý do thiết kế), ClientLoop.cpp (luồng Decode)
// =============================================================================
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <VideoToolbox/VideoToolbox.h>

#include "decode/VtDecoder.h"

#include <cstring>
#include <vector>

#include "deskhubp/Log.h"

namespace {

struct Nal {
    const uint8_t* ptr; // trỏ vào byte NAL header (ngay SAU start code)
    size_t len;         // độ dài NAL, không kể start code
    uint8_t type;       // 5 bit thấp của NAL header
};

// Cắt một frame Annex-B thành danh sách NAL. Start code là 00 00 01 (3 byte) hoặc
// 00 00 00 01 (4 byte). Khi dò NAL kế, bắt gặp start code kế thì dừng — nên độ dài
// NAL không bao giờ nuốt cả start code của gói sau.
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

// Ghi độ dài big-endian 4 byte rồi tới dữ liệu NAL — đúng định dạng AVCC mà
// VideoToolbox đòi (thay cho start code của Annex-B).
void AppendAvcc(std::vector<uint8_t>& out, const uint8_t* nal, size_t len) {
    out.push_back(uint8_t(len >> 24));
    out.push_back(uint8_t(len >> 16));
    out.push_back(uint8_t(len >> 8));
    out.push_back(uint8_t(len));
    out.insert(out.end(), nal, nal + len);
}

} // namespace

VtDecoder::~VtDecoder() {
    Shutdown();
}

bool VtDecoder::Init(void* layer, int width, int height) {
    Shutdown();
    if (!layer) return false;
    layer_ = layer;
    rendered_ = 0;
    // Xoá mốc PTS cũ: sau khi dựng lại decoder, frame đầu tiên mà so với PTS của
    // phiên/lần trước thì mẫu e2e đầu tiên là số rác.
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
        // Nhả hàng đợi của layer để không còn frame cũ nào chờ hiển thị vào một layer
        // ta sắp buông. flush an toàn gọi từ thread Decode.
        AVSampleBufferDisplayLayer* l = (__bridge AVSampleBufferDisplayLayer*)layer_;
        [l flush];
        layer_ = nullptr;
    }
    spsLen_ = ppsLen_ = 0;
}

bool VtDecoder::Decode(const uint8_t* nal, size_t len, uint64_t ptsUs) {
    if (!layer_ || !nal || len == 0) return false;

    const std::vector<Nal> nals = ParseAnnexB(nal, len);

    // Dựng lại formatDesc_ nếu SPS/PPS trong frame khác cái đang dùng. IDR luôn mang
    // SPS/PPS in-band (NVENC repeatSPSPPS); P-frame không có nên giữ nguyên fmt cũ.
    const Nal* sps = nullptr;
    const Nal* pps = nullptr;
    for (const Nal& x : nals) {
        if (x.type == 7)
            sps = &x;
        else if (x.type == 8)
            pps = &x;
    }
    if (sps && pps && (sps->len > sizeof(sps_) || pps->len > sizeof(pps_))) {
        // Tham số to hơn đệm 256 byte — không xảy ra với stream H.264 của ta, nhưng
        // nếu bỏ qua trong im lặng thì formatDesc_ mãi null, Decode cứ trả true và
        // màn hình đen vĩnh viễn không log, không xin IDR. Trả false để ClientLoop
        // dựng lại decoder và lỗi hiện ra trong log.
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

    // Chưa có tham số (frame trước IDR đầu tiên) -> bỏ, KHÔNG phải lỗi.
    if (!formatDesc_) return true;

    // Gom các NAL dữ liệu thành AVCC. Bỏ SPS(7)/PPS(8) — tham số đã nằm trong
    // formatDesc_; bỏ AUD(9) — vài bộ giải mã khó chịu với nó trong sample data.
    avcc_.clear();
    avcc_.reserve(len);
    for (const Nal& x : nals) {
        if (x.type == 7 || x.type == 8 || x.type == 9) continue;
        AppendAvcc(avcc_, x.ptr, x.len);
    }
    if (avcc_.empty()) return true; // frame chỉ có tham số (vd. gói SPS/PPS lẻ)

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

    // Đồng hồ host micro-giây -> timescale 1e6. DisplayImmediately (dưới) khiến layer
    // hiển thị ngay, nhưng PTS vẫn cần để đo e2e và để layer sắp thứ tự nếu dồn gói.
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

    // Đối ứng MF_LOW_LATENCY / khóa "low-latency" của Android: buộc hiển thị ngay,
    // không giữ frame sắp xếp lại thứ tự. Chuỗi của ta không có B-frame nên không
    // mất gì.
    if (CFArrayRef atts = CMSampleBufferGetSampleAttachmentsArray(sb, true)) {
        if (CFArrayGetCount(atts) > 0) {
            CFMutableDictionaryRef d0 =
                (CFMutableDictionaryRef)CFArrayGetValueAtIndex(atts, 0);
            CFDictionarySetValue(d0, kCMSampleAttachmentKey_DisplayImmediately,
                kCFBooleanTrue);
        }
    }

    AVSampleBufferDisplayLayer* l = (__bridge AVSampleBufferDisplayLayer*)layer_;

    // Layer vào trạng thái lỗi (thường sau khi app từ nền quay lại) -> flush rồi báo
    // lỗi để ClientLoop dựng lại decoder và xin IDR. Đây là nếp riêng của iOS so với
    // Android (docs/12 §4).
    if (l.status == AVQueuedSampleBufferRenderingStatusFailed) {
        // %s + UTF8String, KHÔNG phải %@: LOGW là fprintf (Log.h), không phải NSLog —
        // %@ ở đây in ra rác và có thể làm hỏng stack.
        LOGW("[Decoder] display layer failed (%s); flushing.",
            l.error ? l.error.localizedDescription.UTF8String : "unknown");
        [l flush];
        CFRelease(sb);
        return false;
    }

    // Layer nghẽn (chưa tiêu hết mẫu cũ): VỨT frame này thay vì dồn thêm — chính
    // sách của cả pipeline là thà rơi hình còn hơn tăng trễ, và hàng đợi của layer
    // dồn đầy có thể tự chuyển sang trạng thái Failed.
    //
    // ⚠ VỨT FRAME LÀ ĐỨT CHUỖI THAM CHIẾU. Trả true ở đây (decoder vẫn LÀNH, chỉ
    //   đang bận) nhưng phải đếm lại: mọi frame sau đó tham chiếu vào frame vừa bị
    //   bỏ, nên client BẮT BUỘC phải xin IDR — không thì hình lem luốc kéo dài tới
    //   keyframe kế tiếp mà không ai biết vì sao. Bản trước bỏ frame IM LẶNG, không
    //   đếm và không xin gì cả.
    //   Đây KHÔNG phải đường `return false`: cái đó khiến ClientLoop tháo hẳn
    //   decoder và dựng lại — quá nặng cho một cơn nghẽn thoáng qua.
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
