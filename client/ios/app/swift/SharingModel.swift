import Foundation
import Observation

@MainActor @Observable
final class SharingModel {
    static let extensionBundleId = "com.ios.deskhub.broadcast"

    private static let pollInterval = Duration.milliseconds(1000)

    var passcode: String
    var status = BroadcastStatus()
    var addresses: [LocalAddress] = []

    private var lastValidPasscode: String

    init() {
        let stored = dh_settings_load()
        passcode = DeskhubClient.cString(stored.passcode)
        lastValidPasscode = DeskhubClient.cString(stored.passcode)
    }

    var acceptedPasscode: String {
        DeskhubClient.isValidPasscode(passcode) ? passcode : lastValidPasscode
    }

    var statusLine: String {
        let port = UInt16(dh_settings_load().port)
        guard status.sharing else {
            return DeskhubClient.buffered(160) { dh_idle_host_status(port, $0, $1) }
        }
        return DeskhubClient.buffered(320) {
            dh_sharing_status(port, acceptedPasscode, false, $0, $1)
        }
    }

    func savePasscode() {
        guard DeskhubClient.isValidPasscode(passcode) else { return }
        lastValidPasscode = passcode
        let stored = dh_settings_load()
        dh_settings_save(
            stored.fps, stored.bitrateMbps, stored.maxDim, stored.port, stored.allowInput,
            stored.clientControl, passcode
        )
    }

    func poll() async {
        while !Task.isCancelled {
            status = BroadcastStatus.load()
            addresses = LocalAddress.all()
            try? await Task.sleep(for: SharingModel.pollInterval)
        }
    }
}
