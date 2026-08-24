#pragma once
#include <cstddef>
#include <cstdint>

namespace deskhub::media {

inline constexpr uint32_t kAudioSampleRate = 48000;
inline constexpr uint32_t kAudioChannels = 2;
inline constexpr uint32_t kAudioSamplesPerFrame = 960;
inline constexpr uint32_t kAudioBitrateBps = 64000;
inline constexpr uint32_t kAudioExpectedLossPct = 5;

inline constexpr size_t kAudioSamplesPerFrameInterleaved =
    size_t(kAudioSamplesPerFrame) * kAudioChannels;

struct AudioFormat {
    uint32_t sampleRate = kAudioSampleRate;
    uint32_t channels = kAudioChannels;
    uint32_t samplesPerFrame = kAudioSamplesPerFrame;

    size_t interleavedSamples() const {
        return size_t(samplesPerFrame) * channels;
    }

    bool operator==(const AudioFormat&) const = default;
};

inline constexpr bool IsSupportedAudioFormat(const AudioFormat& f) {
    return (f.sampleRate == 8000 || f.sampleRate == 12000 || f.sampleRate == 16000 ||
               f.sampleRate == 24000 || f.sampleRate == 48000) &&
           (f.channels == 1 || f.channels == 2) &&
           f.samplesPerFrame == f.sampleRate / 50;
}

}
