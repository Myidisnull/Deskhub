#pragma once
#include <cstdint>

namespace linuxkeys {

struct KeyEntry {
    uint16_t evdev;
    int32_t vk;
    int32_t scan;
};

inline uint16_t GdkKeycodeToEvdev(uint32_t hardwareKeycode) {
    return hardwareKeycode >= 8 ? uint16_t(hardwareKeycode - 8) : 0;
}

bool EvdevToWin(uint16_t evdevCode, int32_t& vk, int32_t& scan);

bool WinVkToEvdev(int32_t vk, uint16_t& evdevCode);

}
