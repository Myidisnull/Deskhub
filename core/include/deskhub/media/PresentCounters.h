#pragma once
#include <atomic>
#include <cstdint>

namespace deskhub::media {

class PresentCounters {
public:
    void FramePresented(uint64_t ptsUs, uint64_t presentedAtUs) {
        rendered_.fetch_add(1, std::memory_order_relaxed);
        if (ptsUs) {
            lastPtsUs_.store(ptsUs, std::memory_order_relaxed);
            lastAtUs_.store(presentedAtUs, std::memory_order_relaxed);
        }
    }

    void RecordPresentDelayMs(uint32_t ms) {
        presentDelayMs_.store(ms, std::memory_order_relaxed);
    }

    void CountCongestionDrop() {
        congestionDrops_.fetch_add(1, std::memory_order_relaxed);
    }

    uint32_t TakeRenderedCount() {
        return rendered_.exchange(0, std::memory_order_relaxed);
    }

    uint32_t TakePresentDelayMs() {
        return presentDelayMs_.exchange(0, std::memory_order_relaxed);
    }

    uint32_t TakeCongestionDrops() {
        return congestionDrops_.exchange(0, std::memory_order_relaxed);
    }

    uint64_t lastRenderedPtsUs() const {
        return lastPtsUs_.load(std::memory_order_relaxed);
    }

    uint64_t lastRenderedAtUs() const {
        return lastAtUs_.load(std::memory_order_relaxed);
    }

    void Reset() {
        rendered_.store(0, std::memory_order_relaxed);
        presentDelayMs_.store(0, std::memory_order_relaxed);
        congestionDrops_.store(0, std::memory_order_relaxed);
        lastPtsUs_.store(0, std::memory_order_relaxed);
        lastAtUs_.store(0, std::memory_order_relaxed);
    }

private:
    std::atomic<uint32_t> rendered_{0};
    std::atomic<uint32_t> presentDelayMs_{0};
    std::atomic<uint32_t> congestionDrops_{0};
    std::atomic<uint64_t> lastPtsUs_{0};
    std::atomic<uint64_t> lastAtUs_{0};
};

}
