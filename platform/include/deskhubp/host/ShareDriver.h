#pragma once
#include "deskhubp/host/SharingHost.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace deskhubp {

inline constexpr uint32_t kShareStatusPollMs = 500;

enum class ShareDriveState : uint8_t { Starting,
    Running,
    Stopped };

class ShareDriver {
public:
    using UiPost = std::function<void(std::function<void()>)>;
    using StartedHandler = std::function<void(bool started, const std::string& error)>;

    ShareDriver() = default;
    ~ShareDriver() {
        Join();
    }
    ShareDriver(const ShareDriver&) = delete;
    ShareDriver& operator=(const ShareDriver&) = delete;

    void StartAsync(SharingHost& host, const std::vector<ShareSource>& sources,
        const ShareOptions& opt, UiPost postToUi, StartedHandler onStarted) {
        starting_.store(true, std::memory_order_release);
        starter_ = std::thread([this, &host, sources, opt, postToUi = std::move(postToUi),
                                   onStarted = std::move(onStarted)] {
            const bool ok = host.Start(sources, opt);
            const std::string error = ok ? std::string() : host.LastError();
            starting_.store(false, std::memory_order_release);
            postToUi([ok, error, onStarted] { onStarted(ok, error); });
        });
    }

    ShareDriveState Poll(SharingHost& host, std::vector<ShareSourceStatus>& rows) const {
        if (starting_.load(std::memory_order_acquire)) return ShareDriveState::Starting;
        if (!host.running()) return ShareDriveState::Stopped;
        rows = host.Status();
        return ShareDriveState::Running;
    }

    void Join() {
        if (starter_.joinable()) starter_.join();
    }

private:
    std::thread starter_;
    std::atomic<bool> starting_{false};
};

}
