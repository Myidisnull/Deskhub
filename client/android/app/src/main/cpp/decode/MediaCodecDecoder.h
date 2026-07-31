#pragma once
#include <android/native_window.h>
#include <media/NdkMediaCodec.h>

#include <cstddef>
#include <cstdint>

#include "deskhub/media/VideoContract.h"

class MediaCodecDecoder {
public:
    MediaCodecDecoder() = default;
    ~MediaCodecDecoder();
    MediaCodecDecoder(const MediaCodecDecoder&) = delete;
    MediaCodecDecoder& operator=(const MediaCodecDecoder&) = delete;

    bool Init(ANativeWindow* window, int width, int height);
    void Shutdown();
    bool IsOpen() const {
        return codec_ != nullptr;
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
    bool DrainOutput();

    AMediaCodec* codec_ = nullptr;
    bool sentCsd_ = false;
    uint32_t rendered_ = 0;
    uint64_t lastRenderedPtsUs_ = 0;
    uint32_t congestionDrops_ = 0;
};

static_assert(deskhub::media::VideoDecoderLike<MediaCodecDecoder>,
    "MediaCodecDecoder must match the shared decoder signature");
static_assert(deskhub::media::RestartableDecoder<MediaCodecDecoder>,
    "MediaCodecDecoder must be restartable in place (Shutdown + IsOpen)");
static_assert(deskhub::media::RenderCountingDecoder<MediaCodecDecoder>,
    "MediaCodecDecoder counts presented frames itself — the enqueue is async so Decode cannot count them");
static_assert(deskhub::media::CongestionAwareDecoder<MediaCodecDecoder>,
    "MediaCodecDecoder must report frames swallowed by the display layer — that is where disp_drop comes from");
