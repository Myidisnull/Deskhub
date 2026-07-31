#pragma once
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

class LocalInputMonitor {
public:
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
    void Run();
    void Rescan();
    void CloseAll();

    std::thread thread_;
    std::atomic<bool> quit_{false};
    std::atomic<uint64_t> lastUs_{0};
    std::vector<int> fds_;
    bool warnedNoAccess_ = false;
};
