#pragma once
#include <cstddef>
#include <cstdint>

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

    uint32_t TakeRenderedCount();

    uint32_t TakeCongestionDrops() {
        const uint32_t n = congestionDrops_;
        congestionDrops_ = 0;
        return n;
    }

    uint64_t lastRenderedPtsUs() const {
        return lastRenderedPtsUs_;
    }

private:
    void* layer_ = nullptr;
    void* formatDesc_ = nullptr;
    uint32_t rendered_ = 0;
    uint64_t lastRenderedPtsUs_ = 0;
    uint32_t congestionDrops_ = 0;

    uint8_t sps_[256] = {};
    uint8_t pps_[256] = {};
    size_t spsLen_ = 0;
    size_t ppsLen_ = 0;

    std::vector<uint8_t> avcc_;
};

static_assert(deskhub::media::VideoDecoderLike<VtDecoder>,
    "VtDecoder must match the shared decoder signature");
static_assert(deskhub::media::RestartableDecoder<VtDecoder>,
    "VtDecoder must be restartable in place (Shutdown + IsOpen)");
static_assert(deskhub::media::RenderCountingDecoder<VtDecoder>,
    "VtDecoder counts presented frames itself — the enqueue is async so Decode cannot count them");
static_assert(deskhub::media::CongestionAwareDecoder<VtDecoder>,
    "VtDecoder must report frames swallowed by the display layer — that is where disp_drop comes from");
