#pragma once
#include <cstddef>
#include <cstdint>

#include "deskhub/media/PresentCounters.h"
#include "deskhub/media/VideoContract.h"
#include <vector>

class VtDecoder {
public:
    VtDecoder() = default;
    ~VtDecoder();
    VtDecoder(const VtDecoder&) = delete;
    VtDecoder& operator=(const VtDecoder&) = delete;

    bool Init(void* layer, int width, int height);
    void Shutdown();
    bool IsOpen() const {
        return layer_ != nullptr;
    }

    bool Decode(const uint8_t* nal, size_t len, uint64_t ptsUs);

    uint32_t TakeRenderedCount() {
        return counters_.TakeRenderedCount();
    }

    uint32_t TakeCongestionDrops() {
        return counters_.TakeCongestionDrops();
    }

    uint64_t lastRenderedPtsUs() const {
        return counters_.lastRenderedPtsUs();
    }

private:
    void* layer_ = nullptr;
    void* formatDesc_ = nullptr;
    deskhub::media::PresentCounters counters_;

    uint8_t sps_[256] = {};
    uint8_t pps_[256] = {};
    size_t spsLen_ = 0;
    size_t ppsLen_ = 0;

    std::vector<uint8_t> avcc_;
};

static_assert(deskhub::media::EngineDecoder<VtDecoder, void*>,
    "VtDecoder must decode, restart in place, and bind to the layer it is handed at Init");
static_assert(deskhub::media::RenderCountingDecoder<VtDecoder>,
    "VtDecoder counts presented frames itself — the enqueue is async so Decode cannot count them");
static_assert(deskhub::media::CongestionAwareDecoder<VtDecoder>,
    "VtDecoder must report frames swallowed by the display layer — that is where disp_drop comes from");
