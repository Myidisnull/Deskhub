#pragma once
#include <atomic>
#include <cstdint>

namespace deskhub {

class FrameGate {
public:
    bool Admit(uint32_t fps, uint64_t timestampUs);

    void Reset() {
        lastUs_.store(0, std::memory_order_relaxed);
        nextDueUs_.store(0, std::memory_order_relaxed);
    }

    bool hasReference() const {
        return lastUs_.load(std::memory_order_relaxed) != 0;
    }

    uint64_t lastAdmittedUs() const {
        const uint64_t stamped = lastUs_.load(std::memory_order_relaxed);
        return stamped ? stamped - 1 : 0;
    }

private:
    static constexpr uint64_t kJitterToleranceUs = 500;

    std::atomic<uint64_t> lastUs_{0};
    std::atomic<uint64_t> nextDueUs_{0};
};

}
