#include "input/LinuxKeyMap.h"

#include <linux/input-event-codes.h>

#include "deskhub/protocol/Wire.h"

namespace linuxkeys {
namespace {

constexpr int32_t E0 = deskhub::kScanExtended;

constexpr int32_t VK_BACK_ = 0x08, VK_TAB_ = 0x09, VK_RETURN_ = 0x0D;
constexpr int32_t VK_PAUSE_ = 0x13, VK_CAPITAL_ = 0x14, VK_ESCAPE_ = 0x1B, VK_SPACE_ = 0x20;
constexpr int32_t VK_PRIOR_ = 0x21, VK_NEXT_ = 0x22, VK_END_ = 0x23, VK_HOME_ = 0x24;
constexpr int32_t VK_LEFT_ = 0x25, VK_UP_ = 0x26, VK_RIGHT_ = 0x27, VK_DOWN_ = 0x28;
constexpr int32_t VK_SNAPSHOT_ = 0x2C, VK_INSERT_ = 0x2D, VK_DELETE_ = 0x2E;
constexpr int32_t VK_LWIN_ = 0x5B, VK_RWIN_ = 0x5C, VK_APPS_ = 0x5D;
constexpr int32_t VK_NUMPAD0_ = 0x60;
constexpr int32_t VK_MULTIPLY_ = 0x6A, VK_ADD_ = 0x6B, VK_SUBTRACT_ = 0x6D;
constexpr int32_t VK_DECIMAL_ = 0x6E, VK_DIVIDE_ = 0x6F, VK_F1_ = 0x70;
constexpr int32_t VK_NUMLOCK_ = 0x90, VK_SCROLL_ = 0x91;
constexpr int32_t VK_LSHIFT_ = 0xA0, VK_RSHIFT_ = 0xA1, VK_LCONTROL_ = 0xA2;
constexpr int32_t VK_RCONTROL_ = 0xA3, VK_LMENU_ = 0xA4, VK_RMENU_ = 0xA5;
constexpr int32_t VK_OEM_1_ = 0xBA, VK_OEM_PLUS_ = 0xBB, VK_OEM_COMMA_ = 0xBC;
constexpr int32_t VK_OEM_MINUS_ = 0xBD, VK_OEM_PERIOD_ = 0xBE, VK_OEM_2_ = 0xBF;
constexpr int32_t VK_OEM_3_ = 0xC0, VK_OEM_4_ = 0xDB, VK_OEM_5_ = 0xDC;
constexpr int32_t VK_OEM_6_ = 0xDD, VK_OEM_7_ = 0xDE;

const KeyEntry kTable[] = {
    {KEY_ESC, VK_ESCAPE_, 0x01},
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
    {KEY_MINUS, VK_OEM_MINUS_, 0x0C},
    {KEY_EQUAL, VK_OEM_PLUS_, 0x0D},
    {KEY_BACKSPACE, VK_BACK_, 0x0E},
    {KEY_TAB, VK_TAB_, 0x0F},

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
    {KEY_LEFTBRACE, VK_OEM_4_, 0x1A},
    {KEY_RIGHTBRACE, VK_OEM_6_, 0x1B},
    {KEY_ENTER, VK_RETURN_, 0x1C},
    {KEY_LEFTCTRL, VK_LCONTROL_, 0x1D},

    {KEY_A, 'A', 0x1E},
    {KEY_S, 'S', 0x1F},
    {KEY_D, 'D', 0x20},
    {KEY_F, 'F', 0x21},
    {KEY_G, 'G', 0x22},
    {KEY_H, 'H', 0x23},
    {KEY_J, 'J', 0x24},
    {KEY_K, 'K', 0x25},
    {KEY_L, 'L', 0x26},
    {KEY_SEMICOLON, VK_OEM_1_, 0x27},
    {KEY_APOSTROPHE, VK_OEM_7_, 0x28},
    {KEY_GRAVE, VK_OEM_3_, 0x29},
    {KEY_LEFTSHIFT, VK_LSHIFT_, 0x2A},
    {KEY_BACKSLASH, VK_OEM_5_, 0x2B},

    {KEY_Z, 'Z', 0x2C},
    {KEY_X, 'X', 0x2D},
    {KEY_C, 'C', 0x2E},
    {KEY_V, 'V', 0x2F},
    {KEY_B, 'B', 0x30},
    {KEY_N, 'N', 0x31},
    {KEY_M, 'M', 0x32},
    {KEY_COMMA, VK_OEM_COMMA_, 0x33},
    {KEY_DOT, VK_OEM_PERIOD_, 0x34},
    {KEY_SLASH, VK_OEM_2_, 0x35},
    {KEY_RIGHTSHIFT, VK_RSHIFT_, 0x36},
    {KEY_KPASTERISK, VK_MULTIPLY_, 0x37},
    {KEY_LEFTALT, VK_LMENU_, 0x38},
    {KEY_SPACE, VK_SPACE_, 0x39},
    {KEY_CAPSLOCK, VK_CAPITAL_, 0x3A},

    {KEY_F1, VK_F1_ + 0, 0x3B},
    {KEY_F2, VK_F1_ + 1, 0x3C},
    {KEY_F3, VK_F1_ + 2, 0x3D},
    {KEY_F4, VK_F1_ + 3, 0x3E},
    {KEY_F5, VK_F1_ + 4, 0x3F},
    {KEY_F6, VK_F1_ + 5, 0x40},
    {KEY_F7, VK_F1_ + 6, 0x41},
    {KEY_F8, VK_F1_ + 7, 0x42},
    {KEY_F9, VK_F1_ + 8, 0x43},
    {KEY_F10, VK_F1_ + 9, 0x44},
    {KEY_F11, VK_F1_ + 10, 0x57},
    {KEY_F12, VK_F1_ + 11, 0x58},
    {KEY_NUMLOCK, VK_NUMLOCK_, 0x45},
    {KEY_SCROLLLOCK, VK_SCROLL_, 0x46},

    {KEY_KP7, VK_NUMPAD0_ + 7, 0x47},
    {KEY_KP8, VK_NUMPAD0_ + 8, 0x48},
    {KEY_KP9, VK_NUMPAD0_ + 9, 0x49},
    {KEY_KPMINUS, VK_SUBTRACT_, 0x4A},
    {KEY_KP4, VK_NUMPAD0_ + 4, 0x4B},
    {KEY_KP5, VK_NUMPAD0_ + 5, 0x4C},
    {KEY_KP6, VK_NUMPAD0_ + 6, 0x4D},
    {KEY_KPPLUS, VK_ADD_, 0x4E},
    {KEY_KP1, VK_NUMPAD0_ + 1, 0x4F},
    {KEY_KP2, VK_NUMPAD0_ + 2, 0x50},
    {KEY_KP3, VK_NUMPAD0_ + 3, 0x51},
    {KEY_KP0, VK_NUMPAD0_ + 0, 0x52},
    {KEY_KPDOT, VK_DECIMAL_, 0x53},

    {KEY_KPENTER, VK_RETURN_, 0x1C | E0},
    {KEY_RIGHTCTRL, VK_RCONTROL_, 0x1D | E0},
    {KEY_KPSLASH, VK_DIVIDE_, 0x35 | E0},
    {KEY_SYSRQ, VK_SNAPSHOT_, 0x37 | E0},
    {KEY_RIGHTALT, VK_RMENU_, 0x38 | E0},
    {KEY_HOME, VK_HOME_, 0x47 | E0},
    {KEY_UP, VK_UP_, 0x48 | E0},
    {KEY_PAGEUP, VK_PRIOR_, 0x49 | E0},
    {KEY_LEFT, VK_LEFT_, 0x4B | E0},
    {KEY_RIGHT, VK_RIGHT_, 0x4D | E0},
    {KEY_END, VK_END_, 0x4F | E0},
    {KEY_DOWN, VK_DOWN_, 0x50 | E0},
    {KEY_PAGEDOWN, VK_NEXT_, 0x51 | E0},
    {KEY_INSERT, VK_INSERT_, 0x52 | E0},
    {KEY_DELETE, VK_DELETE_, 0x53 | E0},
    {KEY_LEFTMETA, VK_LWIN_, 0x5B | E0},
    {KEY_RIGHTMETA, VK_RWIN_, 0x5C | E0},
    {KEY_COMPOSE, VK_APPS_, 0x5D | E0},
    {KEY_PAUSE, VK_PAUSE_, 0x45},
};

}

bool EvdevToWin(uint16_t evdevCode, int32_t& vk, int32_t& scan) {
    return deskhub::ScancodeTable<uint16_t>(kTable).ToWindows(evdevCode, vk, scan);
}

bool WinVkToEvdev(int32_t vk, uint16_t& evdevCode) {
    return deskhub::ScancodeTable<uint16_t>(kTable).FromWindows(vk, evdevCode);
}

}
