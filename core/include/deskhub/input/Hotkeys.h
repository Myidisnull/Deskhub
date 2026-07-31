#pragma once
#include "deskhub/input/VirtualKeys.h"
#include "deskhub/protocol/Wire.h"

#include <cstdint>
#include <span>

namespace deskhub {

struct Hotkey {
    const char* label = "";
    int32_t vk = 0;
    int32_t scan = 0;
    int32_t modVk = 0;
    int32_t modScan = 0;

    constexpr bool hasModifier() const {
        return modVk != 0;
    }
};

inline constexpr Hotkey kTouchHotkeys[] = {
    {"Esc", kVkEscape, 0x01},
    {"Tab", kVkTab, 0x0F},
    {"Enter", kVkReturn, 0x1C},
    {"↑", kVkUp, 0x48 | kScanExtended},
    {"↓", kVkDown, 0x50 | kScanExtended},
    {"←", kVkLeft, 0x4B | kScanExtended},
    {"→", kVkRight, 0x4D | kScanExtended},
    {"Del", kVkDelete, 0x53 | kScanExtended},
    {"Ctrl+C", 'C', 0x2E, kVkControl, 0x1D},
    {"Ctrl+V", 'V', 0x2F, kVkControl, 0x1D},
};

constexpr std::span<const Hotkey> TouchHotkeys() {
    return kTouchHotkeys;
}

}
