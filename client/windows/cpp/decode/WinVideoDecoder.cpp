#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "decode/WinVideoDecoder.h"

#include "deskhubp/system/Clock.h"

namespace {

constexpr uint32_t kDefaultDecodeFps = 60;

}

bool WinVideoDecoder::Init(WinRenderTarget target, int width, int height) {
    Shutdown();
    if (!target.valid() || width <= 0 || height <= 0) return false;

    target_ = target;

    DecoderConfig cfg;
    cfg.codec = deskhub::media::Codec::H264;
    cfg.width = uint32_t(width);
    cfg.height = uint32_t(height);
    cfg.fps = target_.fps ? target_.fps->load(std::memory_order_relaxed) : 0;
    if (!cfg.fps) cfg.fps = kDefaultDecodeFps;

    decoder_ = CreateDecoder(target_.device, cfg,
        [this](const DecodedFrame& frame) { OnDecoded(frame); });
    return decoder_ != nullptr;
}

void WinVideoDecoder::Shutdown() {
    decoder_.reset();
    lastRenderedPtsUs_ = 0;
    lastRenderedAtUs_ = 0;
}

bool WinVideoDecoder::Decode(const uint8_t* nal, size_t len, uint64_t ptsUs) {
    if (!decoder_) return false;
    return decoder_->Decode(nal, len, ptsUs);
}

void WinVideoDecoder::OnDecoded(const DecodedFrame& frame) {
    uint64_t readyUs = 0;
    if (!target_.renderer->RenderNV12(frame.texture, frame.subresource, frame.width,
            frame.height, &readyUs))
        return;

    ++rendered_;
    const uint64_t nowUs = NowUs();
    if (readyUs) presentDelayMs_ = uint32_t((nowUs - readyUs) / 1000);
    if (frame.timestampUs) {
        lastRenderedPtsUs_ = frame.timestampUs;
        lastRenderedAtUs_ = readyUs ? readyUs : nowUs;
    }
}

uint32_t WinVideoDecoder::TakeRenderedCount() {
    const uint32_t n = rendered_;
    rendered_ = 0;
    return n;
}

uint32_t WinVideoDecoder::TakePresentDelayMs() {
    const uint32_t ms = presentDelayMs_;
    presentDelayMs_ = 0;
    return ms;
}
