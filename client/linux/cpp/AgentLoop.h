#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "deskhub/media/AgentTypes.h"

using AgentSource = deskhub::media::ShareSource;
using AgentOptions = deskhub::media::AgentOptions;
using AgentSourceStatus = deskhub::media::AgentSourceStatus;

class AgentLoop {
public:
    AgentLoop();
    ~AgentLoop();
    AgentLoop(const AgentLoop&) = delete;
    AgentLoop& operator=(const AgentLoop&) = delete;

    bool Start(const std::vector<AgentSource>& sources, const AgentOptions& opt);

    void Stop();

    bool running() const {
        return running_.load(std::memory_order_acquire);
    }

    std::vector<AgentSourceStatus> Status();

    std::string LastError();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::atomic<bool> running_{false};
    std::mutex errMutex_;
    std::string lastError_;
};
