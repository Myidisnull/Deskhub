// =============================================================================
// Hotkeys.swift — bảng phím tắt gửi thẳng sang host.
//
// Bàn phím ảo của iOS KHÔNG có Esc, Tab, F-key hay mũi tên, trong khi macOS/Windows
// bắt trọn bàn phím thật và gửi đi tất. Hàng nút cuộn ngang trong bảng điều khiển
// (StreamView) là đường DUY NHẤT tới những phím đó — nó không phải bản sao của một
// thứ trên desktop, nó là cái giá của việc không có bàn phím thật.
//
// Đây là DỮ LIỆU chứ không phải giao diện, nên nằm riêng: thêm phím là thêm một dòng.
//
// LIÊN QUAN: StreamView.swift (nơi dựng hàng nút), SessionModel.keyTap/keyChord
// =============================================================================
import Foundation

/// Một phím tắt gửi thẳng sang host — bàn phím ảo không có những phím này.
/// `modVk` != 0 -> tổ hợp (giữ phím bổ trợ rồi gõ phím chính): Ctrl+C, Ctrl+V...
/// Thêm phím mới = thêm một dòng: mã phím ảo Windows + scancode US (bit8 = cờ E0
/// cho phím mở rộng như mũi tên/Del).
struct Hotkey {
    let label: String
    let vk: Int32
    let scan: Int32
    var modVk: Int32 = 0
    var modScan: Int32 = 0
}

// Không đưa Alt+Tab/phím Win vào: chúng đổi ngữ cảnh trên máy host, host sẽ ngừng
// nhận input.
let kHotkeys: [Hotkey] = [
    Hotkey(label: "Esc", vk: 0x1B, scan: 0x01),
    Hotkey(label: "Tab", vk: 0x09, scan: 0x0F),
    Hotkey(label: "Enter", vk: 0x0D, scan: 0x1C),
    Hotkey(label: "↑", vk: 0x26, scan: 0x148),
    Hotkey(label: "↓", vk: 0x28, scan: 0x150),
    Hotkey(label: "←", vk: 0x25, scan: 0x14B),
    Hotkey(label: "→", vk: 0x27, scan: 0x14D),
    Hotkey(label: "Del", vk: 0x2E, scan: 0x153),
    Hotkey(label: "Ctrl+C", vk: 0x43, scan: 0x2E, modVk: 0x11, modScan: 0x1D),
    Hotkey(label: "Ctrl+V", vk: 0x56, scan: 0x2F, modVk: 0x11, modScan: 0x1D),
]
