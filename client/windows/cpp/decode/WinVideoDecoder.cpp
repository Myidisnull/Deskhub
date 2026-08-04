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
    counters_.Reset();
    renderFailed_.store(false, std::memory_order_release);
}

bool WinVideoDecoder::Decode(const uint8_t* nal, size_t len, uint64_t ptsUs) {
    if (!decoder_) return false;
    if (!decoder_->Decode(nal, len, ptsUs)) return false;
    return !renderFailed_.exchange(false, std::memory_order_acq_rel);
}

void WinVideoDecoder::OnDecoded(const DecodedFrame& frame) {
    uint64_t readyUs = 0;
    if (!target_.renderer->RenderNV12(frame.texture, frame.subresource, frame.width,
            frame.height, &readyUs)) {
        renderFailed_.store(true, std::memory_order_release);
        return;
    }

    const uint64_t nowUs = NowUs();
    if (readyUs) counters_.RecordPresentDelayMs(uint32_t((nowUs - readyUs) / 1000));
    counters_.FramePresented(frame.timestampUs, readyUs ? readyUs : nowUs);
}
