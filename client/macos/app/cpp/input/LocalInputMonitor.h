#pragma once
#include <atomic>
#include <cstdint>

class LocalInputMonitor {
public:
    static constexpr int64_t kUserData = 0x4445534B'48554200LL;

    static constexpr uint64_t kQuietUs = 1'000'000;

    LocalInputMonitor() = default;
    ~LocalInputMonitor();
    LocalInputMonitor(const LocalInputMonitor&) = delete;
    LocalInputMonitor& operator=(const LocalInputMonitor&) = delete;

    void Start();
    void Stop();

    uint64_t lastLocalUs() const {
        return lastUs_.load(std::memory_order_relaxed);
    }

    bool LocalActive(uint64_t nowUs) const {
        const uint64_t t = lastLocalUs();
        return t != 0 && nowUs - t < kQuietUs;
    }

private:
    std::atomic<uint64_t> lastUs_{0};
    void* monitor_ = nullptr;
};
