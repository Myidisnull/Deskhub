#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <vector>

#include "deskhub/media/ShareSource.h"

std::vector<deskhub::media::ShareSource> ListDisplays();
