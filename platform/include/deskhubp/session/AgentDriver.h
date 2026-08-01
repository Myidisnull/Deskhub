#pragma once
#include "deskhubp/session/AgentLoop.h"

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace deskhubp {

inline constexpr uint32_t kAgentStatusPollMs = 500;

enum class AgentDriveState : uint8_t { Starting,
    Running,
    Stopped };

class AgentDriver {
public:
    using UiPost = std::function<void(std::function<void()>)>;
    using StartedHandler = std::function<void(bool started, const std::string& error)>;

    AgentDriver() = default;
    ~AgentDriver() {
        Join();
    }
    AgentDriver(const AgentDriver&) = delete;
    AgentDriver& operator=(const AgentDriver&) = delete;

    void StartAsync(AgentLoop& agent, const std::vector<AgentSource>& sources,
        const AgentOptions& opt, UiPost postToUi, StartedHandler onStarted) {
        starting_.store(true, std::memory_order_release);
        starter_ = std::thread([this, &agent, sources, opt, postToUi = std::move(postToUi),
                                   onStarted = std::move(onStarted)] {
            const bool ok = agent.Start(sources, opt);
            const std::string error = ok ? std::string() : agent.LastError();
            starting_.store(false, std::memory_order_release);
            postToUi([ok, error, onStarted] { onStarted(ok, error); });
        });
    }

    AgentDriveState Poll(AgentLoop& agent, std::vector<AgentSourceStatus>& rows) const {
        if (starting_.load(std::memory_order_acquire)) return AgentDriveState::Starting;
        if (!agent.running()) return AgentDriveState::Stopped;
        rows = agent.Status();
        return AgentDriveState::Running;
    }

    void Join() {
        if (starter_.joinable()) starter_.join();
    }

private:
    std::thread starter_;
    std::atomic<bool> starting_{false};
};

}
