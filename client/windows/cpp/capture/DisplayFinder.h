#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

struct DisplayInfo {
    HMONITOR monitor = nullptr;
    std::wstring name;
    uint32_t width = 0, height = 0;
    bool primary = false;
};

std::vector<DisplayInfo> ListDisplays();
