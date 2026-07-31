#pragma once
#include <cstdint>

namespace mackeys {

struct KeyEntry {
    uint16_t mac;
    int32_t vk;
    int32_t scan;
};

bool MacToWin(uint16_t macKeyCode, int32_t& vk, int32_t& scan);

bool WinVkToMac(int32_t vk, uint16_t& macKeyCode);

enum class Modifier : uint8_t { None,
    Shift,
    Control,
    Option,
    Command,
    CapsLock };

Modifier ModifierOf(int32_t vk);

}
