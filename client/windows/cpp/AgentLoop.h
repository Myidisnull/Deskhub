#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "capture/ScreenCapture.h"

#include "deskhub/media/ShareSource.h"

struct AgentOptions {
    uint32_t fps = 60;
    uint32_t bitrateMbps = 20;
    uint32_t maxDim = 1920;
};

using AgentSource = deskhub::media::ShareSource;

struct AgentControl;

int RunAgent(std::span<const AgentSource> sources, const AgentOptions& opt, AgentControl& ctl);
