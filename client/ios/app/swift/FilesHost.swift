import Foundation
import Observation

@MainActor @Observable
final class FilesHost {
    static let shared = FilesHost()

    private(set) var receiving = false
    @ObservationIgnored private var activeSettings = ""

    private init() {}

    func run() async {
        while !Task.isCancelled {
            sync()
            try? await Task.sleep(for: .seconds(1))
        }
    }

    func stop() {
        guard receiving else { return }
        dha_stop()
        receiving = false
        activeSettings = ""
    }

    private func sync() {
        let wanted = dh_accept_files() && !BroadcastStatus.load().sharing
        if wanted, receiving, activeSettings != FilesHost.settingsSignature() { stop() }
        if wanted, !receiving { start() }
        if !wanted, receiving { stop() }
        if receiving, !dha_running() { receiving = false }
    }

    private static func settingsSignature() -> String {
        let stored = dh_settings_load()
        return DeskhubClient.cString(stored.passcode) + "@" + String(stored.port)
    }

    private func start() {
        let stored = dh_settings_load()
        let passcode = DeskhubClient.cString(stored.passcode)
        receiving = dha_start_files(UInt16(stored.port), passcode)
        if receiving { activeSettings = FilesHost.settingsSignature() }
    }
}
