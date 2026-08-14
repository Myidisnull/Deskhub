import Foundation
import Observation

@MainActor @Observable
final class SharingModel {
    static let extensionBundleId = "com.ios.deskhub.broadcast"

    private static let pollInterval = Duration.milliseconds(1000)

    var passcode: String
    var status = BroadcastStatus()
    var addresses: [LocalAddress] = []
    var bindIp = DeskhubClient.buffered(64) { dh_bind_ip($0, $1) }
    var encryptSession = dh_encrypt_session()
    var escrowSessionKey = dh_escrow_session_key()
    var sessionKeyLifetime = dh_session_key_lifetime()
    var sessionKeyHex = DeskhubClient.buffered(DH_SESSION_KEY_CAP) { dh_session_key_hex($0, $1) }

    private var lastValidPasscode: String

    init() {
        let stored = dh_settings_load()
        passcode = DeskhubClient.cString(stored.passcode)
        lastValidPasscode = DeskhubClient.cString(stored.passcode)
        if encryptSession {
            _ = dh_ensure_session_key(false)
            sessionKeyHex = DeskhubClient.buffered(DH_SESSION_KEY_CAP) { dh_session_key_hex($0, $1) }
        }
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

    func saveBindIp() {
        dh_set_bind_ip(bindIp)
    }

    func saveEncryptSession() {
        dh_set_encrypt_session(encryptSession)
        if !encryptSession { escrowSessionKey = false }
        dh_set_escrow_session_key(escrowSessionKey)
        if encryptSession {
            _ = dh_ensure_session_key(false)
            sessionKeyHex = DeskhubClient.buffered(DH_SESSION_KEY_CAP) { dh_session_key_hex($0, $1) }
        }
    }

    func saveEscrowSessionKey() {
        if !encryptSession { escrowSessionKey = false }
        dh_set_escrow_session_key(escrowSessionKey)
    }

    func saveSessionKeyLifetime() {
        dh_set_session_key_lifetime(Int32(sessionKeyLifetime))
    }

    func refreshSessionKey() {
        guard encryptSession else { return }
        _ = dh_ensure_session_key(true)
        sessionKeyHex = DeskhubClient.buffered(DH_SESSION_KEY_CAP) { dh_session_key_hex($0, $1) }
    }

    func savePasscode() {
        guard DeskhubClient.isValidPasscode(passcode) else { return }
        lastValidPasscode = passcode
        let stored = dh_settings_load()
        dh_settings_save(
            stored.fps, stored.bitrateMbps, stored.maxDim, stored.port, stored.allowInput,
            stored.clientControl, stored.runInBackground, stored.runInBackgroundChoiceMade,
            stored.hideTrayIcon, stored.shareOnLaunch, stored.logMaxFileMb,
            stored.logCompressAfterDays, stored.logDeleteAfterDays,
            DeskhubClient.cString(stored.logDir), passcode
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
