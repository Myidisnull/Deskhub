import Foundation
import Observation

struct ScanHit: Identifiable, Sendable, Hashable {
    let addr: String
    let ping: String
    var id: String { addr }
}

struct RecentDevice: Identifiable, Sendable, Hashable {
    let addr: String
    let passcode: String
    let status: String
    let ping: String
    let lastConnected: String
    let online: Bool
    var id: String { addr }
}

nonisolated enum DeskhubDiscovery {
    static var defaultPort: UInt16 { dh_default_port() }

    static func startScan(port: UInt16) {
        _ = dh_scan_start(port)
    }

    static func cancelScan() {
        dh_scan_cancel()
    }

    static var isScanning: Bool { dh_scan_state().running }

    static func scanStatus(port: UInt16) -> String {
        DeskhubClient.buffered(256) { dh_scan_status_text(port, $0, $1) }
    }

    static func scanHits() -> [ScanHit] {
        DeskhubClient.ffiList(
            64, DHScanHit(),
            { dh_scan_hits($0, $1) },
            { hit in
                ScanHit(
                    addr: DeskhubClient.cString(hit.addr),
                    ping: DeskhubClient.buffered(32) { dh_ping_text(hit.rttMs, $0, $1) }
                )
            }
        )
    }

    static func recentDevices() -> [RecentDevice] {
        DeskhubClient.ffiList(
            16, DHRecentRow(),
            { dh_recent_rows($0, $1) },
            { row in
                RecentDevice(
                    addr: DeskhubClient.cString(row.addr),
                    passcode: DeskhubClient.cString(row.passcode),
                    status: DeskhubClient.cString(row.status),
                    ping: DeskhubClient.cString(row.ping),
                    lastConnected: DeskhubClient.cString(row.lastConnected),
                    online: row.online
                )
            }
        )
    }

    static func remember(address: String, passcode: String) {
        dh_recent_touch(address, passcode)
        dh_status_watch_recent()
    }

    static func passcode(for address: String) -> String {
        DeskhubClient.buffered(16) { dh_recent_passcode(address, $0, $1) }
    }

    static func watchRecent() {
        dh_status_watch_recent()
    }

    static func stopWatching() {
        dh_status_stop()
    }
}

@MainActor @Observable
final class DiscoveryModel {
    private static let pollInterval = Duration.seconds(1)
    private static let rescanTicks = 45

    var scanHits: [ScanHit] = []
    var recent: [RecentDevice] = []
    var scanStatus = ""

    private var pump: Task<Void, Never>?
    private let port = DeskhubDiscovery.defaultPort

    func start() {
        guard pump == nil else { return }
        pump = Task { [port] in
            await Task.detached {
                DeskhubDiscovery.watchRecent()
                DeskhubDiscovery.startScan(port: port)
            }.value

            var idleTicks = 0
            while !Task.isCancelled {
                let snapshot = await Task.detached {
                    (
                        hits: DeskhubDiscovery.scanHits(),
                        recent: DeskhubDiscovery.recentDevices(),
                        status: DeskhubDiscovery.scanStatus(port: port),
                        scanning: DeskhubDiscovery.isScanning
                    )
                }.value

                scanHits = snapshot.hits
                recent = snapshot.recent
                scanStatus = snapshot.status

                if snapshot.scanning {
                    idleTicks = 0
                } else {
                    idleTicks += 1
                    if idleTicks >= DiscoveryModel.rescanTicks {
                        idleTicks = 0
                        await Task.detached { DeskhubDiscovery.startScan(port: port) }.value
                    }
                }

                try? await Task.sleep(for: DiscoveryModel.pollInterval)
            }
        }
    }

    func stop() {
        pump?.cancel()
        pump = nil
        DeskhubDiscovery.cancelScan()
    }

    func remember(address: String, passcode: String) async {
        await Task.detached { DeskhubDiscovery.remember(address: address, passcode: passcode) }
            .value
        recent = await Task.detached { DeskhubDiscovery.recentDevices() }.value
    }
}
