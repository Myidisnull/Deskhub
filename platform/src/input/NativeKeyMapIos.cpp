#include "deskhubp/input/NativeKeyMap.h"

#include "deskhub/input/ScancodeTable.h"

namespace deskhubp {
namespace {

using namespace deskhub;
using KeyEntry = ScancodeEntry<uint16_t>;

constexpr KeyEntry kTable[] = {
    {0x29, kVkEscape},
    {0x50, kVkLeft},
    {0x52, kVkUp},
    {0x4F, kVkRight},
    {0x51, kVkDown},
    {0x4A, kVkHome},
    {0x4D, kVkEnd},
    {0x4B, kVkPrior},
    {0x4E, kVkNext},
    {0x49, kVkInsert},
    {0x4C, kVkDelete},

    {0x3A, kVkF1 + 0},
    {0x3B, kVkF1 + 1},
    {0x3C, kVkF1 + 2},
    {0x3D, kVkF1 + 3},
    {0x3E, kVkF1 + 4},
    {0x3F, kVkF1 + 5},
    {0x40, kVkF1 + 6},
    {0x41, kVkF1 + 7},
    {0x42, kVkF1 + 8},
    {0x43, kVkF1 + 9},
    {0x44, kVkF1 + 10},
    {0x45, kVkF1 + 11},

    {0xE1, kVkLShift},
    {0xE5, kVkRShift},
    {0xE0, kVkLControl},
    {0xE4, kVkRControl},
    {0xE2, kVkLMenu},
    {0xE6, kVkRMenu},
    {0xE3, kVkLWin},
    {0xE7, kVkRWin},
};

}

bool NativeKeyToWin(int32_t nativeKeyCode, int32_t& vk, int32_t& scan) {
    if (nativeKeyCode < 0 || nativeKeyCode > 0xFFFF) return false;
    return deskhub::ScancodeTable<uint16_t>(kTable).ToWindows(uint16_t(nativeKeyCode), vk, scan);
}

bool WinVkToNative(int32_t vk, int32_t& nativeKeyCode) {
    uint16_t code = 0;
    if (!deskhub::ScancodeTable<uint16_t>(kTable).FromWindows(vk, code)) return false;
    nativeKeyCode = code;
    return true;
}

}
