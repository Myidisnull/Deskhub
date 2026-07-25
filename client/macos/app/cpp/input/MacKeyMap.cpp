// =============================================================================
// MacKeyMap.cpp — bảng ba cột macOS/VK/scancode và hai hàm tra.
//
// CÁCH ĐỌC BẢNG
//   Mỗi dòng là MỘT phím vật lý trên bàn phím US. Ba cột theo đúng thứ tự khai báo
//   ở KeyEntry: mã macOS (Carbon kVK_*), mã phím ảo Windows, scancode PC set 1.
//   Scancode có | kE0 nghĩa là phím "mở rộng" — trên PC nó đi kèm tiền tố 0xE0
//   (mũi tên, Ctrl/Alt phải, cụm Home/End, Enter của bàn phím số).
//
// VÌ SAO TRA TUYẾN TÍNH CHỨ KHÔNG PHẢI BẢNG BĂM / MẢNG 256
//   Bảng chỉ ~110 dòng và mỗi lần tra ứng với một lần người dùng bấm phím — vài
//   chục lần một giây là nhiều. Quét tuyến tính trên mảng liên tục nằm gọn trong
//   cache; đổi lấy một cấu trúc nhanh hơn ở đây là tối ưu hoá thứ không ai đo được.
//
// THỨ TỰ DÒNG CÓ Ý NGHĨA Ở CHIỀU NGƯỢC
//   WinVkToMac quét từ trên xuống và lấy KHỚP ĐẦU TIÊN. Với VK bị trùng (VK_SHIFT
//   chung ứng cả Shift trái lẫn phải) thì dòng đứng trước — bản TRÁI — thắng, đúng
//   quy ước "phím chung = phím trái" của Windows.
//
// LIÊN QUAN: input/MacKeyMap.h (lý do thiết kế + giới hạn layout US)
// =============================================================================
#include "input/MacKeyMap.h"

#include <cstddef>

namespace mackeys {
namespace {

constexpr int32_t kE0 = 0x100; // = deskhub::kScanExtended, chép lại để khỏi kéo Wire.h

// clang-format off
constexpr KeyEntry kTable[] = {
    // --- Chữ cái (VK trùng mã ASCII chữ HOA) ---
    {0x00, 'A', 0x1E}, {0x0B, 'B', 0x30}, {0x08, 'C', 0x2E}, {0x02, 'D', 0x20},
    {0x0E, 'E', 0x12}, {0x03, 'F', 0x21}, {0x05, 'G', 0x22}, {0x04, 'H', 0x23},
    {0x22, 'I', 0x17}, {0x26, 'J', 0x24}, {0x28, 'K', 0x25}, {0x25, 'L', 0x26},
    {0x2E, 'M', 0x32}, {0x2D, 'N', 0x31}, {0x1F, 'O', 0x18}, {0x23, 'P', 0x19},
    {0x0C, 'Q', 0x10}, {0x0F, 'R', 0x13}, {0x01, 'S', 0x1F}, {0x11, 'T', 0x14},
    {0x20, 'U', 0x16}, {0x09, 'V', 0x2F}, {0x0D, 'W', 0x11}, {0x07, 'X', 0x2D},
    {0x10, 'Y', 0x15}, {0x06, 'Z', 0x2C},

    // --- Hàng số (VK trùng mã ASCII chữ số) ---
    {0x1D, '0', 0x0B}, {0x12, '1', 0x02}, {0x13, '2', 0x03}, {0x14, '3', 0x04},
    {0x15, '4', 0x05}, {0x17, '5', 0x06}, {0x16, '6', 0x07}, {0x1A, '7', 0x08},
    {0x1C, '8', 0x09}, {0x19, '9', 0x0A},

    // --- Phím ký hiệu (VK_OEM_*, layout US) ---
    {0x1B, 0xBD, 0x0C}, // - _   VK_OEM_MINUS
    {0x18, 0xBB, 0x0D}, // = +   VK_OEM_PLUS
    {0x21, 0xDB, 0x1A}, // [ {   VK_OEM_4
    {0x1E, 0xDD, 0x1B}, // ] }   VK_OEM_6
    {0x2A, 0xDC, 0x2B}, // \ |   VK_OEM_5
    {0x29, 0xBA, 0x27}, // ; :   VK_OEM_1
    {0x27, 0xDE, 0x28}, // ' "   VK_OEM_7
    {0x32, 0xC0, 0x29}, // ` ~   VK_OEM_3
    {0x2B, 0xBC, 0x33}, // , <   VK_OEM_COMMA
    {0x2F, 0xBE, 0x34}, // . >   VK_OEM_PERIOD
    {0x2C, 0xBF, 0x35}, // / ?   VK_OEM_2

    // --- Phím điều khiển cơ bản ---
    {0x24, 0x0D, 0x1C},   // Return   -> VK_RETURN
    {0x30, 0x09, 0x0F},   // Tab      -> VK_TAB
    {0x31, 0x20, 0x39},   // Space    -> VK_SPACE
    {0x33, 0x08, 0x0E},   // Delete   -> VK_BACK (phím Backspace của Mac)
    {0x35, 0x1B, 0x01},   // Escape   -> VK_ESCAPE
    {0x39, 0x14, 0x3A},   // CapsLock -> VK_CAPITAL

    // --- Phím bổ trợ. Bản TRÁI đứng trước để WinVkToMac chọn nó cho VK chung. ---
    {0x38, 0xA0, 0x2A},        // Shift trái   -> VK_LSHIFT
    {0x3C, 0xA1, 0x36},        // Shift phải   -> VK_RSHIFT
    {0x3B, 0xA2, 0x1D},        // Control trái -> VK_LCONTROL
    {0x3E, 0xA3, 0x1D | kE0},  // Control phải -> VK_RCONTROL
    {0x3A, 0xA4, 0x38},        // Option trái  -> VK_LMENU  (Alt trái)
    {0x3D, 0xA5, 0x38 | kE0},  // Option phải  -> VK_RMENU  (Alt phải)
    {0x37, 0x5B, 0x5B | kE0},  // Command trái -> VK_LWIN
    {0x36, 0x5C, 0x5C | kE0},  // Command phải -> VK_RWIN
    // VK "chung" (0x10/0x11/0x12): không có phím vật lý riêng, nhưng host Windows
    // và bàn phím ảo mobile vẫn gửi chúng. Đặt SAU bản trái/phải nên MacToWin không
    // bao giờ sinh ra chúng, còn WinVkToMac thì dịch được.
    {0x38, 0x10, 0x2A},        // VK_SHIFT   -> Shift trái
    {0x3B, 0x11, 0x1D},        // VK_CONTROL -> Control trái
    {0x3A, 0x12, 0x38},        // VK_MENU    -> Option trái

    // --- Cụm điều hướng (đều là phím mở rộng E0 trên PC) ---
    {0x7B, 0x25, 0x4B | kE0},  // Left
    {0x7C, 0x27, 0x4D | kE0},  // Right
    {0x7E, 0x26, 0x48 | kE0},  // Up
    {0x7D, 0x28, 0x50 | kE0},  // Down
    {0x73, 0x24, 0x47 | kE0},  // Home
    {0x77, 0x23, 0x4F | kE0},  // End
    {0x74, 0x21, 0x49 | kE0},  // PageUp
    {0x79, 0x22, 0x51 | kE0},  // PageDown
    {0x75, 0x2E, 0x53 | kE0},  // ForwardDelete -> VK_DELETE
    {0x72, 0x2D, 0x52 | kE0},  // Help (vị trí phím Insert của PC) -> VK_INSERT

    // --- F1..F20. Mac có F13..F20 trên bàn phím rời; Windows có VK tương ứng. ---
    {0x7A, 0x70, 0x3B}, {0x78, 0x71, 0x3C}, {0x63, 0x72, 0x3D}, {0x76, 0x73, 0x3E},
    {0x60, 0x74, 0x3F}, {0x61, 0x75, 0x40}, {0x62, 0x76, 0x41}, {0x64, 0x77, 0x42},
    {0x65, 0x78, 0x43}, {0x6D, 0x79, 0x44}, {0x67, 0x7A, 0x57}, {0x6F, 0x7B, 0x58},
    {0x69, 0x7C, 0x64}, {0x6B, 0x7D, 0x65}, {0x71, 0x7E, 0x66}, {0x6A, 0x7F, 0x67},
    {0x40, 0x80, 0x68}, {0x4F, 0x81, 0x69}, {0x50, 0x82, 0x6A}, {0x5A, 0x83, 0x6B},

    // --- Bàn phím số ---
    {0x52, 0x60, 0x52}, {0x53, 0x61, 0x4F}, {0x54, 0x62, 0x50}, {0x55, 0x63, 0x51},
    {0x56, 0x64, 0x4B}, {0x57, 0x65, 0x4C}, {0x58, 0x66, 0x4D}, {0x59, 0x67, 0x47},
    {0x5B, 0x68, 0x48}, {0x5C, 0x69, 0x49},
    {0x41, 0x6E, 0x53},        // KP .  -> VK_DECIMAL
    {0x43, 0x6A, 0x37},        // KP *  -> VK_MULTIPLY
    {0x45, 0x6B, 0x4E},        // KP +  -> VK_ADD
    {0x4E, 0x6D, 0x4A},        // KP -  -> VK_SUBTRACT
    {0x4B, 0x6F, 0x35 | kE0},  // KP /  -> VK_DIVIDE
    {0x4C, 0x0D, 0x1C | kE0},  // KP Enter -> VK_RETURN (bản mở rộng)
};
// clang-format on

constexpr size_t kCount = sizeof(kTable) / sizeof(kTable[0]);

} // namespace

bool MacToWin(uint16_t macKeyCode, int32_t& vk, int32_t& scan) {
    for (size_t i = 0; i < kCount; ++i) {
        if (kTable[i].mac != macKeyCode) continue;
        // Bỏ qua ba dòng VK "chung" ở cuối cụm bổ trợ: chúng dùng lại mã macOS của
        // phím trái, nên quét theo mac sẽ trúng dòng TRÁI trước — đúng ý.
        vk = kTable[i].vk;
        scan = kTable[i].scan;
        return true;
    }
    return false;
}

bool WinVkToMac(int32_t vk, uint16_t& macKeyCode) {
    for (size_t i = 0; i < kCount; ++i) {
        if (kTable[i].vk != vk) continue;
        macKeyCode = kTable[i].mac;
        return true;
    }
    return false;
}

Modifier ModifierOf(int32_t vk) {
    switch (vk) {
        case 0x10: // VK_SHIFT
        case 0xA0: // VK_LSHIFT
        case 0xA1: // VK_RSHIFT
            return Modifier::Shift;
        case 0x11: // VK_CONTROL
        case 0xA2: // VK_LCONTROL
        case 0xA3: // VK_RCONTROL
            return Modifier::Control;
        case 0x12: // VK_MENU (Alt)
        case 0xA4: // VK_LMENU
        case 0xA5: // VK_RMENU
            return Modifier::Option;
        case 0x5B: // VK_LWIN
        case 0x5C: // VK_RWIN
            return Modifier::Command;
        case 0x14: // VK_CAPITAL
            return Modifier::CapsLock;
        default:
            return Modifier::None;
    }
}

} // namespace mackeys
