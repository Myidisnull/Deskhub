#pragma once
#include "deskhub/input/Set1Scancodes.h"
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
    {"Esc", kVkEscape, VkToSet1Scancode(kVkEscape)},
    {"Tab", kVkTab, VkToSet1Scancode(kVkTab)},
    {"Enter", kVkReturn, VkToSet1Scancode(kVkReturn)},
    {"↑", kVkUp, VkToSet1Scancode(kVkUp)},
    {"↓", kVkDown, VkToSet1Scancode(kVkDown)},
    {"←", kVkLeft, VkToSet1Scancode(kVkLeft)},
    {"→", kVkRight, VkToSet1Scancode(kVkRight)},
    {"Del", kVkDelete, VkToSet1Scancode(kVkDelete)},
    {"Ctrl+C", 'C', VkToSet1Scancode('C'), kVkControl, VkToSet1Scancode(kVkControl)},
    {"Ctrl+V", 'V', VkToSet1Scancode('V'), kVkControl, VkToSet1Scancode(kVkControl)},
};

constexpr std::span<const Hotkey> TouchHotkeys() {
    return kTouchHotkeys;
}

}
