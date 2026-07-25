// =============================================================================
// DeskhubClient.swift — điểm gọi xuống C++ duy nhất của VAI CLIENT.
//                       Đối ứng client/ios/app/swift/DeskhubClient.swift.
//
// KHÔNG View nào gọi trực tiếp hàm C — mọi lối đi qua đây. Tương lai nếu cần mock
// cho test thì chỉ cần mock lớp này.
// =============================================================================
import AVFoundation

nonisolated enum Phase: Int, Sendable {
    case idle = 0
    case connecting = 1
    case streaming = 2
    case ended = 3
}

// Nút chuột theo deskhub::MouseButton (Wire.h).
nonisolated enum MouseButton: Int32, Sendable {
    case left = 1
    case right = 2
    case middle = 3
}

struct Source: Identifiable, Sendable {
    let id: UInt8
    let width: UInt16
    let height: UInt16
    let name: String
}

nonisolated enum DeskhubClient {
    // CHẶN ~3s — gọi ngoài main thread (Task.detached).
    static func listSources(address: String) -> [Source] {
        var buf = [DHSourceInfo](repeating: DHSourceInfo(), count: 16)
        let count = buf.withUnsafeMutableBufferPointer { ptr in
            dh_list_sources(address, ptr.baseAddress, Int32(ptr.count))
        }
        guard count > 0 else { return [] }
        return (0 ..< Int(count)).map { idx in
            let info = buf[idx]
            let name = withUnsafeBytes(of: info.name) { rawBuf in
                let ptr = rawBuf.baseAddress!.assumingMemoryBound(to: CChar.self)
                return String(cString: ptr)
            }
            return Source(id: info.sourceId, width: info.width, height: info.height, name: name)
        }
    }

    @discardableResult
    static func start(address: String, sourceId: UInt8) -> Bool {
        dh_start(address, sourceId)
    }

    static func stop() {
        dh_stop()
    }

    static func setLayer(_ layer: AVSampleBufferDisplayLayer?) {
        let ptr = layer.map { Unmanaged.passUnretained($0).toOpaque() }
        dh_set_layer(ptr)
    }

    // --- Input ---

    // Nhấn/nhả một phím vật lý. `vk`/`scan` lấy từ mapKey.
    static func key(vk: Int32, scan: Int32, down: Bool) {
        dh_key(vk, scan, down)
    }

    // NSEvent.keyCode -> (VK Windows, scancode PC). nil = phím không dịch được.
    // Bảng nằm ở C++ (input/MacKeyMap.h) và chỉ có MỘT bản — Swift không giữ bản sao.
    static func mapKey(_ macKeyCode: UInt16) -> (vk: Int32, scan: Int32)? {
        var vk: Int32 = 0
        var scan: Int32 = 0
        guard dh_map_key(macKeyCode, &vk, &scan) else { return nil }
        return (vk, scan)
    }

    static func releaseAllInput() {
        dh_release_all_input()
    }

    // Chuột tuyệt đối: toạ độ chuẩn hoá 0..65535 trong khung video.
    static func mouseMove(nx: Int32, ny: Int32) {
        dh_mouse_move(nx, ny)
    }

    // Chuột tương đối — chế độ khoá chuột cho game FPS (F9): delta thô.
    static func mouseMoveRel(dx: Int32, dy: Int32) {
        dh_mouse_move_rel(dx, dy)
    }

    static func mouseButton(_ button: MouseButton, down: Bool) {
        dh_mouse_button(button.rawValue, down)
    }

    // `delta` là bội của 120 (dương = cuộn lên), như WHEEL_DELTA của Windows.
    static func mouseWheel(_ delta: Int32) {
        dh_mouse_wheel(delta)
    }

    // --- Clipboard hai chiều ---

    static func setClipboard(_ text: String) {
        dh_set_clipboard(text)
    }

    // Chuỗi rỗng = host chưa copy gì mới.
    static func takeRemoteClipboard() -> String {
        String(cString: dh_take_remote_clipboard())
    }

    // --- Trạng thái ---

    static func phase() -> Phase {
        Phase(rawValue: Int(dh_phase().rawValue)) ?? .idle
    }

    static func statusLine() -> String {
        String(cString: dh_status_line())
    }

    static func endReason() -> String {
        String(cString: dh_end_reason())
    }

    static func videoWidth() -> UInt32 {
        dh_video_width()
    }

    static func videoHeight() -> UInt32 {
        dh_video_height()
    }
}
