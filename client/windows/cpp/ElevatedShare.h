#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <span>

#include "deskhubp/session/AgentLoop.h"

bool IsProcessElevated();

bool RelaunchElevatedShare(std::span<const AgentSource> sources,
    const AgentOptions& opt, bool& outCancelled);
