#include "deskhubp/input/NativeKeyMap.h"

#include "deskhub/input/Set1Scancodes.h"

namespace deskhubp {

bool NativeKeyToWin(int32_t nativeKeyCode, int32_t& vk, int32_t& scan) {
    if (nativeKeyCode <= 0 || nativeKeyCode > 0xFF) return false;
    vk = nativeKeyCode;
    scan = deskhub::VkToSet1Scancode(vk);
    return true;
}

bool WinVkToNative(int32_t vk, int32_t& nativeKeyCode) {
    if (vk <= 0 || vk > 0xFF) return false;
    nativeKeyCode = vk;
    return true;
}

}
