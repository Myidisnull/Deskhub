#pragma once
#include <cstdint>
#include <string>

struct SessionSourceRow {
    uint8_t sourceId = 0;
    std::wstring label;

    bool operator==(const SessionSourceRow& o) const = default;
};
