import Foundation

struct ShareSource: Identifiable, Hashable, Sendable {
    let rawId: UInt32
    let width: UInt32
    let height: UInt32
    let name: String

    var id: UInt32 { rawId }
}

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
    static var hasScreenRecording: Bool { dh_has_screen_recording() }
    static var hasAccessibility: Bool { dh_has_accessibility() }

    static func openScreenRecordingSettings() { dh_open_screen_recording_settings() }
    static func openAccessibilitySettings() { dh_open_accessibility_settings() }

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

    private static func toRaw(_ source: ShareSource) -> DHShareSource {
        var raw = DHShareSource()
        raw.id = source.rawId
        raw.width = source.width
        raw.height = source.height
        withUnsafeMutableBytes(of: &raw.name) { dst in
            guard let base = dst.baseAddress else { return }
            let bytes = Array(source.name.utf8.prefix(dst.count - 1))
            base.copyMemory(from: bytes, byteCount: bytes.count)
            base.advanced(by: bytes.count).assumingMemoryBound(to: CChar.self).pointee = 0
        }
        return raw
    }

    private static func cString(_ tuple: some Any) -> String {
        withUnsafeBytes(of: tuple) { rawBuf in
            guard let base = rawBuf.baseAddress else { return "" }
            return String(cString: base.assumingMemoryBound(to: CChar.self))
        }
    }
}
