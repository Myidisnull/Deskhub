#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "decode/IVideoDecoder.h"
#include "decode/PanelRenderer.h"

#include "deskhub/media/PresentCounters.h"
#include "deskhub/media/VideoContract.h"

struct WinRenderTarget {
    ID3D11Device* device = nullptr;
    PanelRenderer* renderer = nullptr;
    const std::atomic<uint32_t>* fps = nullptr;

    bool valid() const {
        return device != nullptr && renderer != nullptr;
    }
};

class WinVideoDecoder {
public:
    WinVideoDecoder() = default;
    ~WinVideoDecoder() = default;
    WinVideoDecoder(const WinVideoDecoder&) = delete;
    WinVideoDecoder& operator=(const WinVideoDecoder&) = delete;

    bool Init(WinRenderTarget target, int width, int height);

    void Shutdown();

    bool IsOpen() const {
        return decoder_ != nullptr;
    }

    bool Decode(const uint8_t* nal, size_t len, uint64_t ptsUs);

    uint32_t TakeRenderedCount() {
        return counters_.TakeRenderedCount();
    }

    uint64_t lastRenderedPtsUs() const {
        return counters_.lastRenderedPtsUs();
    }

    uint32_t TakePresentDelayMs() {
        return counters_.TakePresentDelayMs();
    }

    uint64_t lastRenderedAtUs() const {
        return counters_.lastRenderedAtUs();
    }

private:
    void OnDecoded(const DecodedFrame& frame);

    WinRenderTarget target_{};
    std::unique_ptr<IVideoDecoder> decoder_;
    deskhub::media::PresentCounters counters_;
};

static_assert(deskhub::media::EngineDecoder<WinVideoDecoder, WinRenderTarget>,
    "WinVideoDecoder must decode, restart in place, and render into the panel it is handed at "
    "Init");
static_assert(deskhub::media::PresentTimingDecoder<WinVideoDecoder>,
    "the swap chain reports when a frame actually reached the screen");
static_assert(deskhub::media::RenderCountingDecoder<WinVideoDecoder>,
    "the swap chain presents asynchronously, so Decode cannot count presented frames");
