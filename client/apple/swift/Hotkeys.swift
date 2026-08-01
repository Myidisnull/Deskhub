import Foundation

struct Hotkey: Identifiable {
    let label: String
    let vk: Int32
    let scan: Int32
    let modVk: Int32
    let modScan: Int32

    var id: String { label }
}

let kHotkeys: [Hotkey] = {
    var buf = [DHHotkey](repeating: DHHotkey(), count: 32)
    let count = buf.withUnsafeMutableBufferPointer { ptr in
        dh_hotkeys(ptr.baseAddress, Int32(ptr.count))
    }
    guard count > 0 else { return [] }
    return (0 ..< Int(count)).map { idx in
        let entry = buf[idx]
        let label = withUnsafeBytes(of: entry.label) { rawBuf in
            String(cString: rawBuf.baseAddress!.assumingMemoryBound(to: CChar.self))
        }
        return Hotkey(
            label: label, vk: entry.vk, scan: entry.scan,
            modVk: entry.modVk, modScan: entry.modScan
        )
    }
}()
