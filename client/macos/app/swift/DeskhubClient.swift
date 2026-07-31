import AVFoundation

nonisolated enum Phase: Int, Sendable {
    case idle = 0
    case connecting = 1
    case streaming = 2
    case ended = 3
}

nonisolated enum MouseButton: Int32, Sendable {
    case left = 1
    case right = 2
    case middle = 3
}

struct Source: Identifiable, Sendable, Hashable {
    let id: UInt8
    let width: UInt16
    let height: UInt16
    let name: String
}

nonisolated enum DeskhubClient {
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

    static func mapKey(_ macKeyCode: UInt16) -> (vk: Int32, scan: Int32)? {
        var vk: Int32 = 0
        var scan: Int32 = 0
        guard dh_map_key(macKeyCode, &vk, &scan) else { return nil }
        return (vk, scan)
    }
}

final class ClientSession: @unchecked Sendable {
    private let handle: OpaquePointer

    private init(handle: OpaquePointer) {
        self.handle = handle
    }

    static func start(address: String, sourceId: UInt8) -> ClientSession? {
        guard let handle = dh_session_start(address, sourceId) else { return nil }
        return ClientSession(handle: handle)
    }

    func stop() {
        dh_session_stop(handle)
    }

    func setLayer(_ layer: AVSampleBufferDisplayLayer?) {
        let ptr = layer.map { Unmanaged.passUnretained($0).toOpaque() }
        dh_session_set_layer(handle, ptr)
    }

    func key(vk: Int32, scan: Int32, down: Bool) {
        dh_session_key(handle, vk, scan, down)
    }

    func releaseAllInput() {
        dh_session_release_all_input(handle)
    }

    func mouseMove(nx: Int32, ny: Int32) {
        dh_session_mouse_move(handle, nx, ny)
    }

    func mouseMoveRel(dx: Int32, dy: Int32) {
        dh_session_mouse_move_rel(handle, dx, dy)
    }

    func mouseButton(_ button: MouseButton, down: Bool) {
        dh_session_mouse_button(handle, button.rawValue, down)
    }

    func mouseWheel(_ delta: Int32) {
        dh_session_mouse_wheel(handle, delta)
    }

    func phase() -> Phase {
        Phase(rawValue: Int(dh_session_phase(handle).rawValue)) ?? .idle
    }

    func statusLine() -> String {
        String(cString: dh_session_status_line(handle))
    }

    func endReason() -> String {
        String(cString: dh_session_end_reason(handle))
    }

    func videoWidth() -> UInt32 {
        dh_session_video_width(handle)
    }

    func videoHeight() -> UInt32 {
        dh_session_video_height(handle)
    }
}
