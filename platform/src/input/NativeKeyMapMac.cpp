#include "deskhubp/input/NativeKeyMap.h"

#include "deskhub/input/ScancodeTable.h"

namespace deskhubp {
namespace {

using namespace deskhub;
using KeyEntry = ScancodeEntry<uint16_t>;

// clang-format off
constexpr KeyEntry kTable[] = {
    {0x00, 'A'}, {0x0B, 'B'}, {0x08, 'C'}, {0x02, 'D'},
    {0x0E, 'E'}, {0x03, 'F'}, {0x05, 'G'}, {0x04, 'H'},
    {0x22, 'I'}, {0x26, 'J'}, {0x28, 'K'}, {0x25, 'L'},
    {0x2E, 'M'}, {0x2D, 'N'}, {0x1F, 'O'}, {0x23, 'P'},
    {0x0C, 'Q'}, {0x0F, 'R'}, {0x01, 'S'}, {0x11, 'T'},
    {0x20, 'U'}, {0x09, 'V'}, {0x0D, 'W'}, {0x07, 'X'},
    {0x10, 'Y'}, {0x06, 'Z'},

    {0x1D, '0'}, {0x12, '1'}, {0x13, '2'}, {0x14, '3'},
    {0x15, '4'}, {0x17, '5'}, {0x16, '6'}, {0x1A, '7'},
    {0x1C, '8'}, {0x19, '9'},

    {0x1B, kVkOemMinus},
    {0x18, kVkOemPlus},
    {0x21, kVkOem4},
    {0x1E, kVkOem6},
    {0x2A, kVkOem5},
    {0x29, kVkOem1},
    {0x27, kVkOem7},
    {0x32, kVkOem3},
    {0x2B, kVkOemComma},
    {0x2F, kVkOemPeriod},
    {0x2C, kVkOem2},

    {0x24, kVkReturn},
    {0x30, kVkTab},
    {0x31, kVkSpace},
    {0x33, kVkBack},
    {0x35, kVkEscape},
    {0x39, kVkCapital},

    {0x38, kVkLShift},
    {0x3C, kVkRShift},
    {0x3B, kVkLControl},
    {0x3E, kVkRControl},
    {0x3A, kVkLMenu},
    {0x3D, kVkRMenu},
    {0x37, kVkLWin},
    {0x36, kVkRWin},
    {0x38, kVkShift},
    {0x3B, kVkControl},
    {0x3A, kVkMenu},

    {0x7B, kVkLeft},
    {0x7C, kVkRight},
    {0x7E, kVkUp},
    {0x7D, kVkDown},
    {0x73, kVkHome},
    {0x77, kVkEnd},
    {0x74, kVkPrior},
    {0x79, kVkNext},
    {0x75, kVkDelete},
    {0x72, kVkInsert},

    {0x7A, kVkF1 +  0}, {0x78, kVkF1 +  1}, {0x63, kVkF1 +  2},
    {0x76, kVkF1 +  3}, {0x60, kVkF1 +  4}, {0x61, kVkF1 +  5},
    {0x62, kVkF1 +  6}, {0x64, kVkF1 +  7}, {0x65, kVkF1 +  8},
    {0x6D, kVkF1 +  9}, {0x67, kVkF1 + 10}, {0x6F, kVkF1 + 11},
    {0x69, kVkF1 + 12}, {0x6B, kVkF1 + 13}, {0x71, kVkF1 + 14},
    {0x6A, kVkF1 + 15}, {0x40, kVkF1 + 16}, {0x4F, kVkF1 + 17},
    {0x50, kVkF1 + 18}, {0x5A, kVkF1 + 19},

    {0x52, kVkNumpad0 + 0}, {0x53, kVkNumpad0 + 1},
    {0x54, kVkNumpad0 + 2}, {0x55, kVkNumpad0 + 3},
    {0x56, kVkNumpad0 + 4}, {0x57, kVkNumpad0 + 5},
    {0x58, kVkNumpad0 + 6}, {0x59, kVkNumpad0 + 7},
    {0x5B, kVkNumpad0 + 8}, {0x5C, kVkNumpad0 + 9},
    {0x41, kVkDecimal},
    {0x43, kVkMultiply},
    {0x45, kVkAdd},
    {0x4E, kVkSubtract},
    {0x4B, kVkDivide},
    {0x4C, kVkReturn, 0x1C | kScanExtended},
};
// clang-format on

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
