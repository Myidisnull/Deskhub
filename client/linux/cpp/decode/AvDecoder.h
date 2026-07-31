#pragma once
#include <cstddef>
#include <cstdint>

#include "deskhub/media/VideoContract.h"

class VideoSink;

class AvDecoder {
public:
    AvDecoder() = default;
    ~AvDecoder();
    AvDecoder(const AvDecoder&) = delete;
    AvDecoder& operator=(const AvDecoder&) = delete;

    bool Init(VideoSink* sink, int width, int height);
    void Shutdown();
    bool IsOpen() const {
        return ctx_ != nullptr;
    }

    bool Decode(const uint8_t* nal, size_t len, uint64_t ptsUs);

    bool hardware() const {
        return hwDevice_ != nullptr;
    }

private:
    void* ctx_ = nullptr;
    void* packet_ = nullptr;
    void* frame_ = nullptr;
    void* hwDevice_ = nullptr;
    VideoSink* sink_ = nullptr;
};

static_assert(deskhub::media::VideoDecoderLike<AvDecoder>,
    "AvDecoder must match the shared decoder signature");
static_assert(deskhub::media::RestartableDecoder<AvDecoder>,
    "AvDecoder must be restartable in place (Shutdown + IsOpen)");
