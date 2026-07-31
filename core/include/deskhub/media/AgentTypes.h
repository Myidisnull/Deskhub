#pragma once
#include "deskhub/media/ShareSource.h"

#include <cstdint>
#include <string>

namespace deskhub::media {

struct AgentOptions {
    uint32_t fps = 60;
    uint32_t bitrateMbps = 20;
    uint32_t maxDim = 1920;

    int32_t desktopX = 0, desktopY = 0;
    uint32_t desktopW = 0, desktopH = 0;
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

}
