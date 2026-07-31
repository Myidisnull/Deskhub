#include "input/LinuxKeyMap.h"

#include <linux/input-event-codes.h>

#include "deskhub/input/VirtualKeys.h"
#include "deskhub/protocol/Wire.h"

namespace linuxkeys {
namespace {

using namespace deskhub;

constexpr int32_t E0 = kScanExtended;

const KeyEntry kTable[] = {
    {KEY_ESC, kVkEscape, 0x01},
    {KEY_1, '1', 0x02},
    {KEY_2, '2', 0x03},
    {KEY_3, '3', 0x04},
    {KEY_4, '4', 0x05},
    {KEY_5, '5', 0x06},
    {KEY_6, '6', 0x07},
    {KEY_7, '7', 0x08},
    {KEY_8, '8', 0x09},
    {KEY_9, '9', 0x0A},
    {KEY_0, '0', 0x0B},
    {KEY_MINUS, kVkOemMinus, 0x0C},
    {KEY_EQUAL, kVkOemPlus, 0x0D},
    {KEY_BACKSPACE, kVkBack, 0x0E},
    {KEY_TAB, kVkTab, 0x0F},

    {KEY_Q, 'Q', 0x10},
    {KEY_W, 'W', 0x11},
    {KEY_E, 'E', 0x12},
    {KEY_R, 'R', 0x13},
    {KEY_T, 'T', 0x14},
    {KEY_Y, 'Y', 0x15},
    {KEY_U, 'U', 0x16},
    {KEY_I, 'I', 0x17},
    {KEY_O, 'O', 0x18},
    {KEY_P, 'P', 0x19},
    {KEY_LEFTBRACE, kVkOem4, 0x1A},
    {KEY_RIGHTBRACE, kVkOem6, 0x1B},
    {KEY_ENTER, kVkReturn, 0x1C},
    {KEY_LEFTCTRL, kVkLControl, 0x1D},

    {KEY_A, 'A', 0x1E},
    {KEY_S, 'S', 0x1F},
    {KEY_D, 'D', 0x20},
    {KEY_F, 'F', 0x21},
    {KEY_G, 'G', 0x22},
    {KEY_H, 'H', 0x23},
    {KEY_J, 'J', 0x24},
    {KEY_K, 'K', 0x25},
    {KEY_L, 'L', 0x26},
    {KEY_SEMICOLON, kVkOem1, 0x27},
    {KEY_APOSTROPHE, kVkOem7, 0x28},
    {KEY_GRAVE, kVkOem3, 0x29},
    {KEY_LEFTSHIFT, kVkLShift, 0x2A},
    {KEY_BACKSLASH, kVkOem5, 0x2B},

    {KEY_Z, 'Z', 0x2C},
    {KEY_X, 'X', 0x2D},
    {KEY_C, 'C', 0x2E},
    {KEY_V, 'V', 0x2F},
    {KEY_B, 'B', 0x30},
    {KEY_N, 'N', 0x31},
    {KEY_M, 'M', 0x32},
    {KEY_COMMA, kVkOemComma, 0x33},
    {KEY_DOT, kVkOemPeriod, 0x34},
    {KEY_SLASH, kVkOem2, 0x35},
    {KEY_RIGHTSHIFT, kVkRShift, 0x36},
    {KEY_KPASTERISK, kVkMultiply, 0x37},
    {KEY_LEFTALT, kVkLMenu, 0x38},
    {KEY_SPACE, kVkSpace, 0x39},
    {KEY_CAPSLOCK, kVkCapital, 0x3A},

    {KEY_F1, kVkF1 + 0, 0x3B},
    {KEY_F2, kVkF1 + 1, 0x3C},
    {KEY_F3, kVkF1 + 2, 0x3D},
    {KEY_F4, kVkF1 + 3, 0x3E},
    {KEY_F5, kVkF1 + 4, 0x3F},
    {KEY_F6, kVkF1 + 5, 0x40},
    {KEY_F7, kVkF1 + 6, 0x41},
    {KEY_F8, kVkF1 + 7, 0x42},
    {KEY_F9, kVkF1 + 8, 0x43},
    {KEY_F10, kVkF1 + 9, 0x44},
    {KEY_F11, kVkF1 + 10, 0x57},
    {KEY_F12, kVkF1 + 11, 0x58},
    {KEY_NUMLOCK, kVkNumLock, 0x45},
    {KEY_SCROLLLOCK, kVkScroll, 0x46},

    {KEY_KP7, kVkNumpad0 + 7, 0x47},
    {KEY_KP8, kVkNumpad0 + 8, 0x48},
    {KEY_KP9, kVkNumpad0 + 9, 0x49},
    {KEY_KPMINUS, kVkSubtract, 0x4A},
    {KEY_KP4, kVkNumpad0 + 4, 0x4B},
    {KEY_KP5, kVkNumpad0 + 5, 0x4C},
    {KEY_KP6, kVkNumpad0 + 6, 0x4D},
    {KEY_KPPLUS, kVkAdd, 0x4E},
    {KEY_KP1, kVkNumpad0 + 1, 0x4F},
    {KEY_KP2, kVkNumpad0 + 2, 0x50},
    {KEY_KP3, kVkNumpad0 + 3, 0x51},
    {KEY_KP0, kVkNumpad0 + 0, 0x52},
    {KEY_KPDOT, kVkDecimal, 0x53},

    {KEY_KPENTER, kVkReturn, 0x1C | E0},
    {KEY_RIGHTCTRL, kVkRControl, 0x1D | E0},
    {KEY_KPSLASH, kVkDivide, 0x35 | E0},
    {KEY_SYSRQ, kVkSnapshot, 0x37 | E0},
    {KEY_RIGHTALT, kVkRMenu, 0x38 | E0},
    {KEY_HOME, kVkHome, 0x47 | E0},
    {KEY_UP, kVkUp, 0x48 | E0},
    {KEY_PAGEUP, kVkPrior, 0x49 | E0},
    {KEY_LEFT, kVkLeft, 0x4B | E0},
    {KEY_RIGHT, kVkRight, 0x4D | E0},
    {KEY_END, kVkEnd, 0x4F | E0},
    {KEY_DOWN, kVkDown, 0x50 | E0},
    {KEY_PAGEDOWN, kVkNext, 0x51 | E0},
    {KEY_INSERT, kVkInsert, 0x52 | E0},
    {KEY_DELETE, kVkDelete, 0x53 | E0},
    {KEY_LEFTMETA, kVkLWin, 0x5B | E0},
    {KEY_RIGHTMETA, kVkRWin, 0x5C | E0},
    {KEY_COMPOSE, kVkApps, 0x5D | E0},
    {KEY_PAUSE, kVkPause, 0x45},
};

}

bool EvdevToWin(uint16_t evdevCode, int32_t& vk, int32_t& scan) {
    return deskhub::ScancodeTable<uint16_t>(kTable).ToWindows(evdevCode, vk, scan);
}

bool WinVkToEvdev(int32_t vk, uint16_t& evdevCode) {
    return deskhub::ScancodeTable<uint16_t>(kTable).FromWindows(vk, evdevCode);
}

}
