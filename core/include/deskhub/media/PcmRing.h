#pragma once
#include "deskhub/media/AudioTypes.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <vector>

namespace deskhub::media {

inline constexpr size_t kPcmRingFrames = 8;

class PcmRing {
public:
    void Open(const AudioFormat& format) {
        std::lock_guard<std::mutex> lock(mutex_);
        samplesPerFrame_ = format.interleavedSamples();
        ring_.assign(kPcmRingFrames * samplesPerFrame_, 0);
        readAt_ = 0;
        filled_ = 0;
    }

    size_t Take(int16_t* out, size_t wanted) {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) return 0;
        const size_t take = std::min(wanted, filled_);
        for (size_t i = 0; i < take; ++i) {
            out[i] = ring_[readAt_];
            readAt_ = (readAt_ + 1) % ring_.size();
        }
        filled_ -= take;
        return take;
    }

    size_t TakeOrSilence(int16_t* out, size_t wanted) {
        const size_t got = Take(out, wanted);
        if (got < wanted) {
            std::memset(out + got, 0, (wanted - got) * sizeof(int16_t));
            starved_.fetch_add(1, std::memory_order_relaxed);
        }
        return got;
    }

    void Put(std::span<const int16_t> pcm) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (ring_.empty() || pcm.size() > ring_.size()) return;
        while (ring_.size() - filled_ < pcm.size()) {
            const size_t drop = std::min(pcm.size(), filled_);
            readAt_ = (readAt_ + drop) % ring_.size();
            filled_ -= drop;
            dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        size_t writeAt = (readAt_ + filled_) % ring_.size();
        for (int16_t s : pcm) {
            ring_[writeAt] = s;
            writeAt = (writeAt + 1) % ring_.size();
        }
        filled_ += pcm.size();
    }

    size_t framesQueued() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return samplesPerFrame_ != 0 ? filled_ / samplesPerFrame_ : 0;
    }

    uint64_t framesDropped() const {
        return dropped_.load(std::memory_order_relaxed);
    }

    uint64_t framesStarved() const {
        return starved_.load(std::memory_order_relaxed);
    }

private:
    mutable std::mutex mutex_;
    std::vector<int16_t> ring_;
    size_t samplesPerFrame_ = 0;
    size_t readAt_ = 0;
    size_t filled_ = 0;

    std::atomic<uint64_t> dropped_{0};
    std::atomic<uint64_t> starved_{0};
};

}
