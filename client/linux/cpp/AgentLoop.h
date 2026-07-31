#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct AgentOptions {
    uint32_t fps = 60;
    uint32_t bitrateMbps = 20;
    uint32_t maxDim = 1920;

    int32_t desktopX = 0, desktopY = 0;
    uint32_t desktopW = 0, desktopH = 0;
};

struct AgentSource {
    uint32_t nodeId = 0;
    std::string name;
    int32_t x = 0, y = 0;
    uint32_t width = 0, height = 0;
};

struct AgentSourceStatus {
    uint8_t sourceId = 0;
    std::string name;
    uint32_t width = 0, height = 0;
    bool viewerConnected = false;
    std::string viewerAddr;
    double captureFps = 0, sendFps = 0, sendKbps = 0;
    uint32_t rttMs = 0;
    bool zeroCopy = false;
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

    std::string LastError();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::atomic<bool> running_{false};
    std::mutex errMutex_;
    std::string lastError_;
};
