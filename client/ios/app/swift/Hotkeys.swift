import Foundation

struct Hotkey {
    let label: String
    let vk: Int32
    let scan: Int32
    var modVk: Int32 = 0
    var modScan: Int32 = 0
}

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
