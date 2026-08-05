#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace deskhubp {

struct ScanHit {
    std::string addr;
    uint32_t rttMs = 0;
};

struct ScanProgress {
    size_t probed = 0;
    size_t total = 0;
    size_t found = 0;
};

std::vector<uint32_t> LocalScanTargets();

class LanScanner {
public:
    using UiPost = std::function<void(std::function<void()>)>;
    using HitHandler = std::function<void(const ScanHit&)>;
    using ProgressHandler = std::function<void(const ScanProgress&)>;
    using DoneHandler = std::function<void(const ScanProgress&)>;

    LanScanner() = default;
    ~LanScanner();
    LanScanner(const LanScanner&) = delete;
    LanScanner& operator=(const LanScanner&) = delete;

    bool Start(uint16_t port, UiPost postToUi, HitHandler onHit, ProgressHandler onProgress,
        DoneHandler onDone);
    void Cancel();
    bool Busy() const;

private:
    struct State {
        std::atomic<bool> busy{false};
        std::atomic<bool> cancelled{false};
    };

    std::shared_ptr<State> state_ = std::make_shared<State>();
};

}
