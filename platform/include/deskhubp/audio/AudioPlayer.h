#pragma once
#include "deskhub/media/AudioTypes.h"
#include "deskhub/protocol/Wire.h"
#include "deskhub/transport/AudioJitterBuffer.h"
#include "deskhubp/audio/AudioSink.h"
#include "deskhubp/media/OpusCodec.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

namespace deskhubp {

class AudioPlayer {
public:
    struct Stats {
        deskhub::AudioJitterBuffer::Stats jitter{};
        uint64_t decodeFailures = 0;
        uint64_t sinkDropped = 0;
        uint64_t sinkStarved = 0;
    };

    AudioPlayer() = default;
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    bool Start(const deskhub::media::AudioFormat& format = {},
        uint32_t targetDelayMs = deskhub::kDefaultAudioDelayMs);
    void Stop();

    void Push(const deskhub::AudioPacketView& packet);

    bool running() const {
        return running_.load(std::memory_order_acquire);
    }

    Stats stats() const;

private:
    void Run();

    deskhub::media::AudioFormat format_{};
    AudioSink sink_;
    OpusAudioDecoder decoder_;

    mutable std::mutex bufferMutex_;
    deskhub::AudioJitterBuffer buffer_{deskhub::kDefaultAudioDelayMs};

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> quit_{false};
    std::atomic<uint64_t> decodeFailures_{0};
};

}
