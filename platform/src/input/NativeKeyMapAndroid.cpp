#include "deskhubp/input/NativeKeyMap.h"

#include <android/keycodes.h>

#include "deskhub/input/ScancodeTable.h"

namespace deskhubp {
namespace {

using namespace deskhub;
using KeyEntry = ScancodeEntry<uint16_t>;

constexpr KeyEntry kTable[] = {
    {AKEYCODE_ESCAPE, kVkEscape},
    {AKEYCODE_DPAD_LEFT, kVkLeft},
    {AKEYCODE_DPAD_UP, kVkUp},
    {AKEYCODE_DPAD_RIGHT, kVkRight},
    {AKEYCODE_DPAD_DOWN, kVkDown},
    {AKEYCODE_MOVE_HOME, kVkHome},
    {AKEYCODE_MOVE_END, kVkEnd},
    {AKEYCODE_PAGE_UP, kVkPrior},
    {AKEYCODE_PAGE_DOWN, kVkNext},
    {AKEYCODE_INSERT, kVkInsert},
    {AKEYCODE_FORWARD_DEL, kVkDelete},

    {AKEYCODE_F1, kVkF1 + 0},
    {AKEYCODE_F2, kVkF1 + 1},
    {AKEYCODE_F3, kVkF1 + 2},
    {AKEYCODE_F4, kVkF1 + 3},
    {AKEYCODE_F5, kVkF1 + 4},
    {AKEYCODE_F6, kVkF1 + 5},
    {AKEYCODE_F7, kVkF1 + 6},
    {AKEYCODE_F8, kVkF1 + 7},
    {AKEYCODE_F9, kVkF1 + 8},
    {AKEYCODE_F10, kVkF1 + 9},
    {AKEYCODE_F11, kVkF1 + 10},
    {AKEYCODE_F12, kVkF1 + 11},

    {AKEYCODE_SHIFT_LEFT, kVkLShift},
    {AKEYCODE_SHIFT_RIGHT, kVkRShift},
    {AKEYCODE_CTRL_LEFT, kVkLControl},
    {AKEYCODE_CTRL_RIGHT, kVkRControl},
    {AKEYCODE_ALT_LEFT, kVkLMenu},
    {AKEYCODE_ALT_RIGHT, kVkRMenu},
    {AKEYCODE_META_LEFT, kVkLWin},
    {AKEYCODE_META_RIGHT, kVkRWin},
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
