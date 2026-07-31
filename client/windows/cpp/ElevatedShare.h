#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <span>
#include <string>
#include <vector>

#include "AgentLoop.h"

bool IsProcessElevated();

bool RelaunchElevatedShare(std::span<const AgentSource> sources,
    const AgentOptions& opt, bool& outCancelled);

bool ParseElevatedShareArgs(int adeskhub, wchar_t** argv,
    std::vector<AgentSource>& outSources, AgentOptions& outOpt);
