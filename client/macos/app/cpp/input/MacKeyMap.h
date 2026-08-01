#pragma once
#include <cstdint>

#include "deskhub/input/ScancodeTable.h"

namespace mackeys {

using KeyEntry = deskhub::ScancodeEntry<uint16_t>;

bool MacToWin(uint16_t macKeyCode, int32_t& vk, int32_t& scan);

bool WinVkToMac(int32_t vk, uint16_t& macKeyCode);

}
