// =============================================================================
// DeskhubAgent.swift — điểm gọi xuống C++ duy nhất của VAI HOST (chia sẻ máy này).
//                      Không có bản đối ứng bên iOS: iOS không host được
//                      (docs/11 §3).
//
// Cùng quy ước với DeskhubClient.swift: không View nào gọi thẳng hàm C.
// =============================================================================
import Foundation

// Một màn hình máy này chia sẻ được. `id` là CGDirectDisplayID (share theo cửa sổ
// đã bỏ 2026-07-27, nguồn chỉ còn là màn hình).
struct ShareSource: Identifiable, Hashable, Sendable {
    let rawId: UInt32
    let width: UInt32
    let height: UInt32
    let name: String

    var id: UInt32 { rawId }
}

// Trạng thái một nguồn đang chia sẻ, cho màn "Deskhub - sharing" vẽ. Đối ứng
// SessionSourceRow bên Windows — cùng các trường có cấu trúc.
struct AgentSourceStatus: Identifiable, Sendable {
    let id: UInt8
    let name: String
    let width: UInt32
    let height: UInt32
    let viewerConnected: Bool
    let viewerAddr: String
    let captureFps: Double
    let sendFps: Double
    let sendKbps: Double
    let rttMs: UInt32
}

nonisolated enum DeskhubAgent {
    // --- Quyền hệ thống. Thiếu là HỎNG IM LẶNG, xem agent/Permissions.h. ---

    static var hasScreenRecording: Bool { dh_has_screen_recording() }
    static var hasAccessibility: Bool { dh_has_accessibility() }

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
                width: info.width,
                height: info.height,
                name: cString(info.name)
            )
        }
    }

    // --- Phiên chia sẻ ---

    // CHẶN tới ~10s (đợi frame đầu của từng nguồn) — gọi ngoài main thread.
    // Chữ ký chép 1-1 từ C API dha_start; gom thành struct chỉ thêm một lớp vỏ
    // cho đúng một call site. Không có tham số cổng / cho-phép-điều-khiển: cổng
    // luôn 47777 và chuột+bàn phím luôn được chia sẻ (chốt 2026-07-27).
    @discardableResult
    static func start(
        sources: [ShareSource],
        fps: UInt32,
        bitrateMbps: UInt32,
        maxDim: UInt32
    ) -> Bool {
        var raw = sources.map(toRaw)
        return raw.withUnsafeMutableBufferPointer { ptr in
            dha_start(ptr.baseAddress, Int32(ptr.count), fps, bitrateMbps, maxDim)
        }
    }

    static func stop() { dha_stop() }
    static var isRunning: Bool { dha_running() }

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
                viewerAddr: cString(row.viewerAddr),
                captureFps: row.captureFps,
                sendFps: row.sendFps,
                sendKbps: row.sendKbps,
                rttMs: row.rttMs
            )
        }
    }

    // Địa chỉ IPv4 của máy này cho hộp Host mode. Gọi được cả khi chưa chia sẻ —
    // bridge hỏi thẳng hệ thống (mỗi dòng "ip\ttên card").
    static func localAddresses() -> [LocalAddress] {
        String(cString: dha_local_addresses())
            .split(separator: "\n")
            .compactMap { line in
                let parts = line.split(separator: "\t", maxSplits: 1)
                guard let ip = parts.first else { return nil }
                let name = parts.count > 1 ? String(parts[1]) : ""
                return LocalAddress(ip: String(ip), name: name)
            }
    }

    // Không có addSource/removeSource: phiên chia sẻ tất cả màn hình và danh sách
    // chốt lúc start (bỏ 2026-07-27).

    // --- Tiện ích chuyển đổi ---

    private static func toRaw(_ source: ShareSource) -> DHShareSource {
        var raw = DHShareSource()
        raw.id = source.rawId
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
