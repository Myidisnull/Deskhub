#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <vector>

#include "deskhub/protocol/Wire.h"

bool ShowSourcePickerDialog(HWND owner, const std::vector<deskhub::SourceInfo>& sources,
    std::vector<deskhub::SourceInfo>& outSelected);
