#pragma once
#include <cstdint>

namespace deskhubp {

bool NativeKeyToWin(int32_t nativeKeyCode, int32_t& vk, int32_t& scan);

bool WinVkToNative(int32_t vk, int32_t& nativeKeyCode);

}
