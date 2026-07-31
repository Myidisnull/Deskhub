#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "capture/CaptureTypes.h"

struct AgentOptions {
    uint32_t fps = 60;
    uint32_t bitrateMbps = 20;
    uint32_t maxDim = 1920;
};

struct AgentSource {
    uint32_t displayId = 0;
    std::string name;
};

struct AgentSourceStatus {
    uint8_t sourceId = 0;
    std::string name;
    uint32_t width = 0, height = 0;
    bool viewerConnected = false;
    std::string viewerAddr;
    double captureFps = 0, sendFps = 0, sendKbps = 0;
    uint32_t rttMs = 0;
};

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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::atomic<bool> running_{false};
};
