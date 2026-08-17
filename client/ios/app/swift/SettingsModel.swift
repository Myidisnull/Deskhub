import Foundation
import Observation

@MainActor @Observable
final class SettingsModel {
    var port: Int
    var clientControl: Bool
    var clipboardSync: Bool
    var keepAwake: Bool
    var language: AppLanguage

    init() {
        let stored = dh_settings_load()
        port = Int(stored.port)
        clientControl = stored.clientControl
        clipboardSync = dh_clipboard_sync()
        keepAwake = dh_keep_awake()
        language = AppLanguage.fromStored(DeskhubClient.buffered(32) { dh_language($0, $1) })
    }

    var acceptedPort: UInt16 {
        port >= 1 && port <= 65535 ? UInt16(port) : DeskhubDiscovery.defaultPort
    }

    func save() {
        let stored = dh_settings_load()
        dh_settings_save(
            stored.fps, stored.bitrateMbps, stored.maxDim, UInt32(acceptedPort),
            stored.allowInput, clientControl, stored.runInBackground,
            stored.runInBackgroundChoiceMade, stored.hideTrayIcon, stored.shareOnLaunch,
            stored.logMaxFileMb, stored.logCompressAfterDays, stored.logDeleteAfterDays,
            DeskhubClient.cString(stored.logDir), nil
        )
        dh_set_clipboard_sync(clipboardSync)
        dh_set_keep_awake(keepAwake)
        dh_set_language(language.rawValue)
    }
}
