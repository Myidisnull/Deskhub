#include "input/LinuxKeyMap.h"

#include <linux/input-event-codes.h>

#include "deskhub/input/VirtualKeys.h"
#include "deskhub/protocol/Wire.h"

namespace linuxkeys {
namespace {

using namespace deskhub;

const KeyEntry kTable[] = {
    {KEY_ESC, kVkEscape},
    {KEY_1, '1'},
    {KEY_2, '2'},
    {KEY_3, '3'},
    {KEY_4, '4'},
    {KEY_5, '5'},
    {KEY_6, '6'},
    {KEY_7, '7'},
    {KEY_8, '8'},
    {KEY_9, '9'},
    {KEY_0, '0'},
    {KEY_MINUS, kVkOemMinus},
    {KEY_EQUAL, kVkOemPlus},
    {KEY_BACKSPACE, kVkBack},
    {KEY_TAB, kVkTab},

    {KEY_Q, 'Q'},
    {KEY_W, 'W'},
    {KEY_E, 'E'},
    {KEY_R, 'R'},
    {KEY_T, 'T'},
    {KEY_Y, 'Y'},
    {KEY_U, 'U'},
    {KEY_I, 'I'},
    {KEY_O, 'O'},
    {KEY_P, 'P'},
    {KEY_LEFTBRACE, kVkOem4},
    {KEY_RIGHTBRACE, kVkOem6},
    {KEY_ENTER, kVkReturn},
    {KEY_LEFTCTRL, kVkLControl},

    {KEY_A, 'A'},
    {KEY_S, 'S'},
    {KEY_D, 'D'},
    {KEY_F, 'F'},
    {KEY_G, 'G'},
    {KEY_H, 'H'},
    {KEY_J, 'J'},
    {KEY_K, 'K'},
    {KEY_L, 'L'},
    {KEY_SEMICOLON, kVkOem1},
    {KEY_APOSTROPHE, kVkOem7},
    {KEY_GRAVE, kVkOem3},
    {KEY_LEFTSHIFT, kVkLShift},
    {KEY_BACKSLASH, kVkOem5},

    {KEY_Z, 'Z'},
    {KEY_X, 'X'},
    {KEY_C, 'C'},
    {KEY_V, 'V'},
    {KEY_B, 'B'},
    {KEY_N, 'N'},
    {KEY_M, 'M'},
    {KEY_COMMA, kVkOemComma},
    {KEY_DOT, kVkOemPeriod},
    {KEY_SLASH, kVkOem2},
    {KEY_RIGHTSHIFT, kVkRShift},
    {KEY_KPASTERISK, kVkMultiply},
    {KEY_LEFTALT, kVkLMenu},
    {KEY_SPACE, kVkSpace},
    {KEY_CAPSLOCK, kVkCapital},

    {KEY_F1, kVkF1 + 0},
    {KEY_F2, kVkF1 + 1},
    {KEY_F3, kVkF1 + 2},
    {KEY_F4, kVkF1 + 3},
    {KEY_F5, kVkF1 + 4},
    {KEY_F6, kVkF1 + 5},
    {KEY_F7, kVkF1 + 6},
    {KEY_F8, kVkF1 + 7},
    {KEY_F9, kVkF1 + 8},
    {KEY_F10, kVkF1 + 9},
    {KEY_F11, kVkF1 + 10},
    {KEY_F12, kVkF1 + 11},
    {KEY_NUMLOCK, kVkNumLock},
    {KEY_SCROLLLOCK, kVkScroll},

    {KEY_KP7, kVkNumpad0 + 7},
    {KEY_KP8, kVkNumpad0 + 8},
    {KEY_KP9, kVkNumpad0 + 9},
    {KEY_KPMINUS, kVkSubtract},
    {KEY_KP4, kVkNumpad0 + 4},
    {KEY_KP5, kVkNumpad0 + 5},
    {KEY_KP6, kVkNumpad0 + 6},
    {KEY_KPPLUS, kVkAdd},
    {KEY_KP1, kVkNumpad0 + 1},
    {KEY_KP2, kVkNumpad0 + 2},
    {KEY_KP3, kVkNumpad0 + 3},
    {KEY_KP0, kVkNumpad0 + 0},
    {KEY_KPDOT, kVkDecimal},

    {KEY_KPENTER, kVkReturn, 0x1C | kScanExtended},
    {KEY_RIGHTCTRL, kVkRControl},
    {KEY_KPSLASH, kVkDivide},
    {KEY_SYSRQ, kVkSnapshot},
    {KEY_RIGHTALT, kVkRMenu},
    {KEY_HOME, kVkHome},
    {KEY_UP, kVkUp},
    {KEY_PAGEUP, kVkPrior},
    {KEY_LEFT, kVkLeft},
    {KEY_RIGHT, kVkRight},
    {KEY_END, kVkEnd},
    {KEY_DOWN, kVkDown},
    {KEY_PAGEDOWN, kVkNext},
    {KEY_INSERT, kVkInsert},
    {KEY_DELETE, kVkDelete},
    {KEY_LEFTMETA, kVkLWin},
    {KEY_RIGHTMETA, kVkRWin},
    {KEY_COMPOSE, kVkApps},
    {KEY_PAUSE, kVkPause},
};

}

bool EvdevToWin(uint16_t evdevCode, int32_t& vk, int32_t& scan) {
    return deskhub::ScancodeTable<uint16_t>(kTable).ToWindows(evdevCode, vk, scan);
}

bool WinVkToEvdev(int32_t vk, uint16_t& evdevCode) {
    return deskhub::ScancodeTable<uint16_t>(kTable).FromWindows(vk, evdevCode);
}

}
