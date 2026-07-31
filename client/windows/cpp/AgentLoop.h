#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "capture/ScreenCapture.h"

struct AgentOptions {
    uint32_t fps = 60;
    uint32_t bitrateMbps = 20;
    uint32_t maxDim = 1920;
};

struct AgentSource {
    HMONITOR monitor = nullptr;
    std::string name;
};

struct AgentControl;

int RunAgent(std::span<const AgentSource> sources, const AgentOptions& opt, AgentControl& ctl);
