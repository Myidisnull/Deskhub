#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "capture/ScreenCapture.h"

#include "deskhub/media/AgentTypes.h"

using AgentSource = deskhub::media::ShareSource;
using AgentOptions = deskhub::media::AgentOptions;

struct AgentControl;

int RunAgent(std::span<const AgentSource> sources, const AgentOptions& opt, AgentControl& ctl);
