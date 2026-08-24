#pragma once
#include "deskhub/media/AudioTypes.h"

#include <cstdint>
#include <memory>
#include <span>

namespace deskhubp {

class AudioSink {
public:
    AudioSink();
    ~AudioSink();
    AudioSink(const AudioSink&) = delete;
    AudioSink& operator=(const AudioSink&) = delete;

    bool Open(const deskhub::media::AudioFormat& format);
    void Close();

    bool Write(std::span<const int16_t> pcm);

    bool IsOpen() const;

    size_t framesQueued() const;

    uint64_t framesDropped() const;
    uint64_t framesStarved() const;

    static const char* BackendName();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
