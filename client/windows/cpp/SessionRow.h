#pragma once
#include <cstdint>
#include <string>

struct SessionSourceRow {
    uint8_t sourceId = 0;
    std::wstring label;

    std::wstring name;
    uint32_t width = 0, height = 0;
    bool viewerConnected = false;
    std::string viewerAddr;
    uint32_t fps = 0;
    uint32_t kbps = 0;
    uint32_t rttMs = 0;

    uint64_t monitor = 0;

    bool operator==(const SessionSourceRow& o) const = default;
};
