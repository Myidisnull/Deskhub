import Foundation
import Observation

@MainActor @Observable
final class SettingsModel {
    var port: Int
    var clientControl: Bool

    private let fps: UInt32
    private let bitrateMbps: UInt32
    private let maxDim: UInt32
    private let allowInput: Bool
    private let passcode: String

    init() {
        let stored = dh_settings_load()
        fps = stored.fps
        bitrateMbps = stored.bitrateMbps
        maxDim = stored.maxDim
        allowInput = stored.allowInput
        passcode = DeskhubClient.cString(stored.passcode)
        port = Int(stored.port)
        clientControl = stored.clientControl
    }

    var acceptedPort: UInt16 {
        port >= 1 && port <= 65535 ? UInt16(port) : DeskhubDiscovery.defaultPort
    }

    func save() {
        dh_settings_save(
            fps, bitrateMbps, maxDim, UInt32(acceptedPort), allowInput, clientControl, passcode
        )
    }
}
