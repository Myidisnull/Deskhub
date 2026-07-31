#pragma once
#include <cstdint>

#include "deskhub/input/ScancodeTable.h"

namespace linuxkeys {

using KeyEntry = deskhub::ScancodeEntry<uint16_t>;

inline uint16_t GdkKeycodeToEvdev(uint32_t hardwareKeycode) {
    return hardwareKeycode >= 8 ? uint16_t(hardwareKeycode - 8) : 0;
}

bool EvdevToWin(uint16_t evdevCode, int32_t& vk, int32_t& scan);

bool WinVkToEvdev(int32_t vk, uint16_t& evdevCode);

}
