#pragma once
#include <cstdint>
#include <memory>

class LocalInputMonitor {
public:
    static constexpr uint64_t kQuietUs = 1'000'000;

    static constexpr int64_t kInjectedUserData = 0x4445534B'48554200LL;

    LocalInputMonitor();
    ~LocalInputMonitor();
    LocalInputMonitor(const LocalInputMonitor&) = delete;
    LocalInputMonitor& operator=(const LocalInputMonitor&) = delete;

    void Start();
    void Stop();

    uint64_t lastLocalUs() const;

    bool LocalActive(uint64_t nowUs) const {
        const uint64_t t = lastLocalUs();
        return t != 0 && nowUs - t < kQuietUs;
    }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
