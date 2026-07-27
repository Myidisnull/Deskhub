// =============================================================================
// DeskhubClient.swift — điểm gọi xuống C++ duy nhất của VAI CLIENT.
//                       Đối ứng client/ios/app/swift/DeskhubClient.swift, nhưng
//                       theo mô hình HANDLE của Windows (DeskhubApi.h): mỗi phiên
//                       xem là một ClientSession riêng, mở song song được — mỗi
//                       cửa sổ xem một phiên, như Viewer.cpp.
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

struct Source: Identifiable, Sendable, Hashable {
    let id: UInt8
    let width: UInt16
    let height: UInt16
    let name: String
}

// MARK: - Tiện ích không thuộc phiên nào

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

    // NSEvent.keyCode -> (VK Windows, scancode PC). nil = phím không dịch được.
    // Bảng nằm ở C++ (input/MacKeyMap.h) và chỉ có MỘT bản — Swift không giữ bản sao.
    static func mapKey(_ macKeyCode: UInt16) -> (vk: Int32, scan: Int32)? {
        var vk: Int32 = 0
        var scan: Int32 = 0
        guard dh_map_key(macKeyCode, &vk, &scan) else { return nil }
        return (vk, scan)
    }
}

// MARK: - Một phiên xem

// Bọc một handle DHSession (một ClientLoop). Đối ứng DhClientHandle bên Windows.
//
// @unchecked Sendable vì phải tạo trong Task.detached (start chặn ~1s) rồi giao về
// MainActor. An toàn: handle bất biến sau init, các hàm C tự lo thread của chúng, và
// StreamModel bảo đảm không gọi gì sau stop() (nó buông tham chiếu ngay).
final class ClientSession: @unchecked Sendable {
    private let handle: OpaquePointer

    private init(handle: OpaquePointer) {
        self.handle = handle
    }

    // CHẶN ~1s — gọi ngoài main thread. nil = địa chỉ sai / không mở được phiên.
    static func start(address: String, sourceId: UInt8) -> ClientSession? {
        guard let h = dh_session_start(address, sourceId) else { return nil }
        return ClientSession(handle: h)
    }

    // Dừng phiên và giải phóng handle. Gọi đúng MỘT lần; sau đó buông tham chiếu.
    func stop() {
        dh_session_stop(handle)
    }

    func setLayer(_ layer: AVSampleBufferDisplayLayer?) {
        let ptr = layer.map { Unmanaged.passUnretained($0).toOpaque() }
        dh_session_set_layer(handle, ptr)
    }

    // --- Input ---

    func key(vk: Int32, scan: Int32, down: Bool) {
        dh_session_key(handle, vk, scan, down)
    }

    func releaseAllInput() {
        dh_session_release_all_input(handle)
    }

    // Chuột tuyệt đối: toạ độ chuẩn hoá 0..65535 trong khung video.
    func mouseMove(nx: Int32, ny: Int32) {
        dh_session_mouse_move(handle, nx, ny)
    }

    // Chuột tương đối — chế độ khoá chuột cho game FPS (F9): delta thô.
    func mouseMoveRel(dx: Int32, dy: Int32) {
        dh_session_mouse_move_rel(handle, dx, dy)
    }

    func mouseButton(_ button: MouseButton, down: Bool) {
        dh_session_mouse_button(handle, button.rawValue, down)
    }

    // `delta` là bội của 120 (dương = cuộn lên), như WHEEL_DELTA của Windows.
    func mouseWheel(_ delta: Int32) {
        dh_session_mouse_wheel(handle, delta)
    }

    // --- Trạng thái ---

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
