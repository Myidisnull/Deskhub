#include "decode/AvDecoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/opt.h>
}

#include "deskhubp/Log.h"
#include "render/VideoSink.h"

namespace {

AVPixelFormat PickFormat(AVCodecContext* ctx, const AVPixelFormat* fmts) {
    for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
        if (*p == AV_PIX_FMT_VAAPI) return *p;
    LOGW("[Decoder] VA-API not offered for this stream — falling back to software decoding.");
    (void)ctx;
    return fmts[0];
}

}

AvDecoder::~AvDecoder() {
    Shutdown();
}

bool AvDecoder::Init(VideoSink* sink, int width, int height) {
    Shutdown();
    sink_ = sink;

    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        LOGE("[Decoder] libavcodec has no H.264 decoder.");
        return false;
    }

    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    if (!ctx) return false;
    ctx_ = ctx;

    ctx->width = width;
    ctx->height = height;
    ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    ctx->flags2 |= AV_CODEC_FLAG2_FAST;
    ctx->thread_count = 1;
    ctx->thread_type = FF_THREAD_SLICE;

    AVBufferRef* hw = nullptr;
    if (av_hwdevice_ctx_create(&hw, AV_HWDEVICE_TYPE_VAAPI, nullptr, nullptr, 0) == 0) {
        hwDevice_ = hw;
        ctx->hw_device_ctx = av_buffer_ref(hw);
        ctx->get_format = PickFormat;
        auto* devCtx = reinterpret_cast<AVHWDeviceContext*>(hw->data);
        auto* vaCtx = static_cast<AVVAAPIDeviceContext*>(devCtx->hwctx);
        if (sink_) sink_->SetVaDisplay(vaCtx->display);
        LOGI("[Decoder] H.264 via VA-API (hardware).");
    } else {
        if (sink_) sink_->SetVaDisplay(nullptr);
        LOGW("[Decoder] No VA-API decode device — decoding on the CPU.");
    }

    if (avcodec_open2(ctx, codec, nullptr) < 0) {
        LOGE("[Decoder] avcodec_open2 failed.");
        Shutdown();
        return false;
    }

    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    if (!packet_ || !frame_) {
        Shutdown();
        return false;
    }
    return true;
}

void AvDecoder::Shutdown() {
    if (sink_) sink_->DropFrames();

    if (packet_) {
        auto* p = static_cast<AVPacket*>(packet_);
        av_packet_free(&p);
        packet_ = nullptr;
    }
    if (frame_) {
        auto* f = static_cast<AVFrame*>(frame_);
        av_frame_free(&f);
        frame_ = nullptr;
    }
    if (ctx_) {
        auto* c = static_cast<AVCodecContext*>(ctx_);
        avcodec_free_context(&c);
        ctx_ = nullptr;
    }
    if (hwDevice_) {
        auto* h = static_cast<AVBufferRef*>(hwDevice_);
        av_buffer_unref(&h);
        hwDevice_ = nullptr;
    }
    if (sink_) sink_->SetVaDisplay(nullptr);
}

bool AvDecoder::Decode(const uint8_t* nal, size_t len, uint64_t ptsUs) {
    if (!IsOpen() || !nal || !len) return false;

    auto* ctx = static_cast<AVCodecContext*>(ctx_);
    auto* pkt = static_cast<AVPacket*>(packet_);
    auto* frame = static_cast<AVFrame*>(frame_);

    pkt->data = const_cast<uint8_t*>(nal);
    pkt->size = int(len);
    pkt->pts = int64_t(ptsUs);
    pkt->dts = int64_t(ptsUs);

    const int sent = avcodec_send_packet(ctx, pkt);
    if (sent < 0 && sent != AVERROR(EAGAIN)) {
        char err[128] = {};
        av_strerror(sent, err, sizeof(err));
        LOGW("[Decoder] send_packet failed: %s", err);
        return false;
    }

    for (;;) {
        const int got = avcodec_receive_frame(ctx, frame);
        if (got == AVERROR(EAGAIN) || got == AVERROR_EOF) break;
        if (got < 0) {
            char err[128] = {};
            av_strerror(got, err, sizeof(err));
            LOGW("[Decoder] receive_frame failed: %s", err);
            return false;
        }

        AVFrame* out = av_frame_clone(frame);
        av_frame_unref(frame);
        if (!out) continue;
        const uint64_t pts = out->pts != AV_NOPTS_VALUE ? uint64_t(out->pts) : ptsUs;
        if (sink_)
            sink_->SubmitFrame(out, pts);
        else
            av_frame_free(&out);
    }
    return true;
}
