import Foundation

struct Hotkey {
    let label: String
    let vk: Int32
    let scan: Int32
    let modVk: Int32
    let modScan: Int32
}

let kHotkeys: [Hotkey] = DeskhubClient.ffiList(32, DHHotkey(), { dh_hotkeys($0, $1) }) { entry in
    Hotkey(
        label: DeskhubClient.cString(entry.label), vk: entry.vk, scan: entry.scan,
        modVk: entry.modVk, modScan: entry.modScan
    )
}
