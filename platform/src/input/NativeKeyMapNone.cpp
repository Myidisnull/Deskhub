#include "deskhubp/input/NativeKeyMap.h"

namespace deskhubp {

bool NativeKeyToWin(int32_t, int32_t&, int32_t&) {
    return false;
}

bool WinVkToNative(int32_t, int32_t&) {
    return false;
}

}
