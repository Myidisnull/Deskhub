#pragma once
// =============================================================================
// MacKeyMap.h — bảng phím DUY NHẤT của app macOS: mã phím macOS ↔ (VK, scancode)
//               Windows. Dùng chung cho CẢ HAI VAI.
//
// VÌ SAO PHẢI CÓ FILE NÀY
//   Giao thức Deskhub nói bằng NGÔN NGỮ WINDOWS: InputEvent::a là mã phím ảo (VK)
//   Windows, InputEvent::b là scancode PC set 1 (bit8 = cờ E0) — xem Wire.h. Đó là
//   quyết định của bản tham chiếu và KHÔNG đổi được, vì host Windows cần đúng
//   scancode để game DirectInput nhìn thấy phím (docs/07 §5). macOS thì nói bằng
//   virtual keycode của Carbon (kVK_ANSI_A = 0, kVK_Return = 0x24...). Hai hệ mã
//   không liên quan gì nhau, nên phải có một bảng dịch — và chỉ MỘT bảng, ở đây.
//
// HAI CHIỀU, HAI VAI
//   Vai CLIENT (macOS điều khiển máy khác): NSEvent.keyCode → MacToWin() → gửi đi.
//   Vai AGENT  (máy khác điều khiển macOS): nhận VK → WinVkToMac() → CGEvent.
//   Bảng kTable bên .cpp phục vụ cả hai; không có bản sao thứ hai nào để lệch nhau.
//
// VÌ SAO GỬI CẢ scancode DÙ HOST CÓ THỂ TỰ TRA
//   KeyMap.h của core (bàn phím ảo mobile) gửi scan = 0 và để host tự tra theo
//   layout của nó. Client macOS thì KHÁC: nó có bàn phím vật lý thật, biết chính
//   xác phím nào bị bấm, nên gửi thẳng scancode là đường chắc nhất cho game — đúng
//   như client Windows lấy scancode từ Raw Input.
//
// GIỚI HẠN CÓ CHỦ Ý: LAYOUT US
//   Cùng giới hạn với deskhub::CharToKeyChord (core/include/deskhub/input/KeyMap.h).
//   Chữ cái và số luôn đúng; phím ký hiệu (OEM) đúng khi cả hai máy dùng layout US.
//   Người dùng layout khác gõ ký hiệu có thể ra sai phím — chấp nhận ở bản đầu.
//
// LIÊN QUAN: deskhub/wire/Wire.h (InputEvent, kScanExtended),
//            deskhub/input/KeyMap.h (bảng ký tự → VK của core),
//            agent/InputInjector.h (chiều VK → macOS),
//            swift/InputCaptureView.swift (chiều macOS → VK)
// =============================================================================
#include <cstdint>

namespace mackeys {

// Một phím trên bàn phím vật lý, ba hệ mã của nó.
struct KeyEntry {
    uint16_t mac; // virtual keycode macOS (kVK_*)
    int32_t vk;   // mã phím ảo Windows
    int32_t scan; // scancode PC set 1; | deskhub::kScanExtended (0x100) nếu là phím E0
};

// macOS keycode → (VK, scancode). Trả false nếu phím không có trong bảng (phím
// media, Fn, phím của bàn phím lạ) — caller bỏ qua, không gửi gì.
bool MacToWin(uint16_t macKeyCode, int32_t& vk, int32_t& scan);

// VK Windows → macOS keycode. Trả false nếu không dịch được. Nhận cả VK "chung"
// (VK_SHIFT 0x10) lẫn VK trái/phải (VK_LSHIFT 0xA0) — bản chung quy về phím TRÁI.
bool WinVkToMac(int32_t vk, uint16_t& macKeyCode);

// --- Phân loại phím bổ trợ, để InputInjector dựng CGEventFlags ---
// (CGEvent đòi cờ modifier đi kèm MỖI event; thiếu nó thì Shift+A ra 'a'.)
enum class Modifier : uint8_t { None,
    Shift,
    Control,
    Option,  // Alt bên Windows
    Command, // phím Win bên Windows
    CapsLock };

// Phím này là phím bổ trợ nào (theo VK Windows)? None nếu là phím thường.
Modifier ModifierOf(int32_t vk);

} // namespace mackeys
