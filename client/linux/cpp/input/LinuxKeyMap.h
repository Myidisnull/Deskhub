#pragma once
// =============================================================================
// LinuxKeyMap.h — bảng phím DUY NHẤT của app Ubuntu: mã phím evdev ↔ (VK, scancode)
//                 Windows. Dùng chung cho CẢ HAI VAI.
//                 Đối ứng client/macos/.../input/MacKeyMap.h.
//
// VÌ SAO PHẢI CÓ FILE NÀY
//   Giao thức Deskhub nói bằng NGÔN NGỮ WINDOWS: InputEvent::a là mã phím ảo (VK)
//   Windows, InputEvent::b là scancode PC set 1 (bit8 = cờ E0) — xem Wire.h. Đó là
//   quyết định của bản tham chiếu và KHÔNG đổi được, vì host Windows cần đúng
//   scancode để game DirectInput nhìn thấy phím (docs/07 §5). Linux thì nói bằng
//   mã phím evdev (KEY_A = 30, KEY_ENTER = 28...).
//
// ⚠ MỘT SỰ TRÙNG HỢP RẤT TIỆN — VÀ GIỚI HẠN CỦA NÓ
//   Với các phím "thường" (mã 1..88), mã evdev TRÙNG với scancode PC set 1: đó là
//   di sản lịch sử, driver bàn phím AT của Linux ánh xạ thẳng. Nhưng KHÔNG được
//   dựa vào đó mà bỏ bảng đi:
//     - Các phím mở rộng (mũi tên, Ctrl phải, phím Win, cụm Home/End, Enter của
//       numpad) có mã evdev RIÊNG, không liên quan gì tới scancode E0 của chúng.
//       KEY_UP là 103, còn scancode là E0 48.
//     - Chiều VK thì không có quy luật nào cả.
//   Nên bảng kTable bên .cpp là nguồn sự thật; sự trùng hợp chỉ giúp đọc bảng dễ
//   hiểu hơn, không phải để rút gọn nó.
//
// HAI CHIỀU, HAI VAI
//   Vai CLIENT (Ubuntu điều khiển máy khác): GDK hardware_keycode → EvdevToWin()
//     → gửi đi.
//   Vai AGENT  (máy khác điều khiển Ubuntu): nhận VK → WinVkToEvdev() → uinput.
//   Cả hai dùng chung kTable; không có bản sao thứ hai nào để lệch nhau.
//
// ⚠ GDK CỘNG 8 VÀO MÃ EVDEV
//   GdkEventKey::hardware_keycode theo quy ước X11: keycode = mã evdev + 8. Quy
//   ước đó được GDK giữ nguyên CẢ TRÊN WAYLAND để ứng dụng cũ không phải sửa. Hàm
//   GdkKeycodeToEvdev() dưới đây là chỗ DUY NHẤT trong app trừ đi số 8 — đừng rải
//   phép trừ đó ra các call site.
//
// GIỚI HẠN CÓ CHỦ Ý: LAYOUT US
//   Cùng giới hạn với MacKeyMap và với deskhub::CharToKeyChord (core). Chữ cái và
//   số luôn đúng; phím ký hiệu (OEM) đúng khi cả hai máy dùng layout US. Người
//   dùng layout khác gõ ký hiệu có thể ra sai phím — chấp nhận ở bản đầu.
//
// LIÊN QUAN: deskhub/protocol/Wire.h (InputEvent, kScanExtended),
//            input/InputInjector.h (chiều VK → evdev),
//            gtk/ViewerWindow.cpp (chiều evdev → VK),
//            client/macos/app/cpp/input/MacKeyMap.h (bản song song)
// =============================================================================
#include <cstdint>

namespace linuxkeys {

// Một phím trên bàn phím vật lý, ba hệ mã của nó.
struct KeyEntry {
    uint16_t evdev; // mã phím Linux (linux/input-event-codes.h, KEY_*)
    int32_t vk;     // mã phím ảo Windows
    int32_t scan;   // scancode PC set 1; | deskhub::kScanExtended (0x100) nếu là phím E0
};

// GdkEventKey::hardware_keycode → mã evdev. Xem ⚠ ở đầu file.
inline uint16_t GdkKeycodeToEvdev(uint32_t hardwareKeycode) {
    return hardwareKeycode >= 8 ? uint16_t(hardwareKeycode - 8) : 0;
}

// evdev → (VK, scancode). Trả false nếu phím không có trong bảng (phím media,
// Fn, phím của bàn phím lạ) — caller bỏ qua, không gửi gì.
bool EvdevToWin(uint16_t evdevCode, int32_t& vk, int32_t& scan);

// VK Windows → evdev. Trả false nếu không dịch được. Nhận cả VK "chung"
// (VK_SHIFT 0x10) lẫn VK trái/phải (VK_LSHIFT 0xA0) — bản chung quy về phím TRÁI,
// giống MacKeyMap.
bool WinVkToEvdev(int32_t vk, uint16_t& evdevCode);

} // namespace linuxkeys
