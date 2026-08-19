#pragma once
#include "deskhub/media/AudioTypes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>

class AudioCapture {
public:
    using FrameHandler = std::function<void(std::span<const int16_t>)>;

    AudioCapture();
    ~AudioCapture();
    AudioCapture(const AudioCapture&) = delete;
    AudioCapture& operator=(const AudioCapture&) = delete;

    bool Start(const deskhub::media::AudioFormat& format, FrameHandler onFrame);
    void Stop();

    bool Running() const;

    uint64_t framesCaptured() const;
    uint64_t framesPaddedWithSilence() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
