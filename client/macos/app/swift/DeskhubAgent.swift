import Foundation

struct ShareSource: Identifiable, Hashable, Sendable {
    let rawId: UInt32
    let width: UInt32
    let height: UInt32
    let name: String

    var id: UInt32 { rawId }
}

struct ShareOptions: Sendable {
    let fps: UInt32
    let bitrateMbps: UInt32
    let maxDim: UInt32
    let port: UInt16
    let allowInput: Bool
    let passcode: String
}

struct AgentSourceStatus: Identifiable, Sendable {
    let id: UInt8
    let name: String
    let size: String
    let viewerCount: UInt32
    let viewerAddr: String
    let captureFps: String
    let sendFps: String
    let sendMbps: String
    let rtt: String
    let viewerConnected: Bool
}

struct QualityPreset: Identifiable, Sendable {
    let label: String
    let maxDim: Int

    var id: Int { maxDim }
}

nonisolated enum DeskhubAgent {
    static let qualityPresets: [QualityPreset] =
        DeskhubClient.ffiList(
            8, DHQualityPreset(),
            { dha_quality_presets($0, $1) },
            { raw in QualityPreset(label: DeskhubClient.cString(raw.label), maxDim: Int(raw.maxDim)) }
        )

    static var hasScreenRecording: Bool { dh_has_screen_recording() }
    static var hasAccessibility: Bool { dh_has_accessibility() }

    static func requestScreenRecording() -> Bool { dh_request_screen_recording() }
    static func requestAccessibility() -> Bool { dh_request_accessibility() }

    static func openScreenRecordingSettings() { dh_open_screen_recording_settings() }
    static func openAccessibilitySettings() { dh_open_accessibility_settings() }

    static func listShareSources() -> [ShareSource] {
        DeskhubClient.ffiList(
            128, DHShareSource(),
            { dha_list_share_sources($0, $1) },
            { info in
                ShareSource(
                    rawId: info.id,
                    width: info.width,
                    height: info.height,
                    name: cString(info.name)
                )
            }
        )
    }

    @discardableResult
    static func start(sources: [ShareSource], options: ShareOptions) -> Bool {
        var raw = sources.map(toRaw)
        return raw.withUnsafeMutableBufferPointer { ptr in
            dha_start(
                ptr.baseAddress, Int32(ptr.count), options.fps, options.bitrateMbps,
                options.maxDim, options.port, options.allowInput, options.passcode
            )
        }
    }

    static func stop() { dha_stop() }
    static var isRunning: Bool { dha_running() }

    static func status() -> [AgentSourceStatus] {
        DeskhubClient.ffiList(
            16, DHAgentStatus(),
            { dha_status($0, $1) },
            { row in
                AgentSourceStatus(
                    id: row.sourceId,
                    name: cString(row.name),
                    size: "\(row.width)x\(row.height)",
                    viewerCount: row.viewerCount,
                    viewerAddr: cString(row.viewerAddr),
                    captureFps: String(format: "%.0f", row.captureFps),
                    sendFps: String(format: "%.0f", row.sendFps),
                    sendMbps: String(format: "%.1f", row.sendKbps / 1000.0),
                    rtt: row.viewerConnected
                        ? DeskhubClient.buffered(32) { dh_ping_text(row.rttMs, $0, $1) } : "-",
                    viewerConnected: row.viewerConnected
                )
            }
        )
    }

    static var lastError: String { String(cString: dha_last_error()) }

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
        DeskhubClient.cString(tuple)
    }
}
