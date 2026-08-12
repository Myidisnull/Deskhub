import Foundation
import Observation

@MainActor @Observable
final class SettingsModel {
    var port: Int
    var clientControl: Bool

    init() {
        let stored = dh_settings_load()
        port = Int(stored.port)
        clientControl = stored.clientControl
    }

    var acceptedPort: UInt16 {
        port >= 1 && port <= 65535 ? UInt16(port) : DeskhubDiscovery.defaultPort
    }

    func save() {
        let stored = dh_settings_load()
        dh_settings_save(
            stored.fps, stored.bitrateMbps, stored.maxDim, UInt32(acceptedPort),
            stored.allowInput, clientControl, nil
        )
    }
}
