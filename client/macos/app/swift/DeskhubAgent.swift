// =============================================================================
// DeskhubAgent.swift — điểm gọi xuống C++ duy nhất của VAI HOST (chia sẻ máy này).
//                      Không có bản đối ứng bên iOS: iOS không host được
//                      (docs/11 §3).
//
// Cùng quy ước với DeskhubClient.swift: không View nào gọi thẳng hàm C.
// =============================================================================
import Foundation

// Một thứ máy này chia sẻ được. `id` là CGWindowID hoặc CGDirectDisplayID —
// Identifiable phải phân biệt được cả hai, nên khoá gồm cả cờ isDisplay (một cửa sổ
// và một màn hình hoàn toàn có thể trùng số).
struct ShareSource: Identifiable, Hashable, Sendable {
    let rawId: UInt32
    let isDisplay: Bool
    let width: UInt32
    let height: UInt32
    let name: String

    var id: String { "\(isDisplay ? "d" : "w")\(rawId)" }
}

// Trạng thái một nguồn đang chia sẻ, cho ShareView vẽ.
struct AgentSourceStatus: Identifiable, Sendable {
    let id: UInt8
    let name: String
    let width: UInt32
    let height: UInt32
    let viewerConnected: Bool
    let starting: Bool
    let captureFps: Double
    let sendFps: Double
    let sendKbps: Double
}

nonisolated enum DeskhubAgent {
    // --- Quyền hệ thống. Thiếu là HỎNG IM LẶNG, xem agent/Permissions.h. ---

    static var hasScreenRecording: Bool { dh_has_screen_recording() }
    static var hasAccessibility: Bool { dh_has_accessibility() }

    @discardableResult
    static func requestScreenRecording() -> Bool { dh_request_screen_recording() }

    @discardableResult
    static func requestAccessibility() -> Bool { dh_request_accessibility() }

    static func openScreenRecordingSettings() { dh_open_screen_recording_settings() }
    static func openAccessibilitySettings() { dh_open_accessibility_settings() }

    // --- Nguồn chia sẻ ---

    // CHẶN ~2s — gọi ngoài main thread.
    static func listShareSources() -> [ShareSource] {
        var buf = [DHShareSource](repeating: DHShareSource(), count: 128)
        let count = buf.withUnsafeMutableBufferPointer { ptr in
            dha_list_share_sources(ptr.baseAddress, Int32(ptr.count))
        }
        guard count > 0 else { return [] }
        return (0 ..< Int(count)).map { idx in
            let info = buf[idx]
            return ShareSource(
                rawId: info.id,
                isDisplay: info.isDisplay,
                width: info.width,
                height: info.height,
                name: cString(info.name)
            )
        }
    }

    // --- Phiên chia sẻ ---

    // CHẶN tới ~10s (đợi frame đầu của từng nguồn) — gọi ngoài main thread.
    @discardableResult
    static func start(
        sources: [ShareSource],
        port: UInt16,
        fps: UInt32,
        bitrateMbps: UInt32,
        allowInput: Bool,
        shareClipboard: Bool
    ) -> Bool {
        var raw = sources.map(toRaw)
        return raw.withUnsafeMutableBufferPointer { ptr in
            dha_start(ptr.baseAddress, Int32(ptr.count), port, fps, bitrateMbps, allowInput,
                      shareClipboard)
        }
    }

    static func stop() { dha_stop() }
    static var isRunning: Bool { dha_running() }
    static var port: UInt16 { dha_port() }
    static func statusLine() -> String { String(cString: dha_status_line()) }

    static func status() -> [AgentSourceStatus] {
        var buf = [DHAgentStatus](repeating: DHAgentStatus(), count: 16)
        let count = buf.withUnsafeMutableBufferPointer { ptr in
            dha_status(ptr.baseAddress, Int32(ptr.count))
        }
        guard count > 0 else { return [] }
        return (0 ..< Int(count)).map { idx in
            let row = buf[idx]
            return AgentSourceStatus(
                id: row.sourceId,
                name: cString(row.name),
                width: row.width,
                height: row.height,
                viewerConnected: row.viewerConnected,
                starting: row.starting,
                captureFps: row.captureFps,
                sendFps: row.sendFps,
                sendKbps: row.sendKbps
            )
        }
    }

    // Địa chỉ IPv4 của máy này để đọc cho người bên kia.
    static func localAddresses() -> [String] {
        String(cString: dha_local_addresses())
            .split(separator: "\n")
            .map(String.init)
    }

    static func addSource(_ source: ShareSource) {
        var raw = toRaw(source)
        dha_add_source(&raw)
    }

    static func removeSource(id: UInt8) {
        dha_remove_source(id)
    }

    // --- Tiện ích chuyển đổi ---

    private static func toRaw(_ source: ShareSource) -> DHShareSource {
        var raw = DHShareSource()
        raw.id = source.rawId
        raw.isDisplay = source.isDisplay
        raw.width = source.width
        raw.height = source.height
        // Mảng char cố định trong struct C: Swift phơi nó ra thành tuple, không có
        // cách gán chuỗi trực tiếp — phải chép byte qua con trỏ thô.
        withUnsafeMutableBytes(of: &raw.name) { dst in
            guard let base = dst.baseAddress else { return }
            let bytes = Array(source.name.utf8.prefix(dst.count - 1))
            base.copyMemory(from: bytes, byteCount: bytes.count)
            base.advanced(by: bytes.count).assumingMemoryBound(to: CChar.self).pointee = 0
        }
        return raw
    }

    // Mảng char[256] của C được Swift phơi thành TUPLE 256 phần tử, không phải chuỗi.
    // Generic vì mỗi struct C có một kiểu tuple riêng dù cùng cỡ.
    private static func cString(_ tuple: some Any) -> String {
        withUnsafeBytes(of: tuple) { rawBuf in
            guard let base = rawBuf.baseAddress else { return "" }
            return String(cString: base.assumingMemoryBound(to: CChar.self))
        }
    }
}
