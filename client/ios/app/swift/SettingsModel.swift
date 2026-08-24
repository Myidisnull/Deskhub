import Foundation
import Observation

@MainActor @Observable
final class SettingsModel {
    var port: Int
    var clientControl: Bool
    var clipboardSync: Bool
    var shareAudio: Bool
    var playAudio: Bool
    var keepAwake: Bool
    var language: AppLanguage
    var logMaxFileMb: Int
    var logCompressAfterDays: Int
    var logDeleteAfterDays: Int

    init() {
        let stored = dh_settings_load()
        port = Int(stored.port)
        clientControl = stored.clientControl
        clipboardSync = dh_clipboard_sync()
        shareAudio = dh_share_audio()
        playAudio = dh_play_audio()
        keepAwake = dh_keep_awake()
        language = AppLanguage.fromStored(DeskhubClient.buffered(32) { dh_language($0, $1) })
        logMaxFileMb = Int(stored.logMaxFileMb)
        logCompressAfterDays = Int(stored.logCompressAfterDays)
        logDeleteAfterDays = Int(stored.logDeleteAfterDays)
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
            UInt32(max(1, logMaxFileMb)), UInt32(max(0, logCompressAfterDays)),
            UInt32(max(0, logDeleteAfterDays)),
            DeskhubClient.cString(stored.logDir), nil
        )
        dh_set_clipboard_sync(clipboardSync)
        dh_set_share_audio(shareAudio)
        dh_set_play_audio(playAudio)
        dh_set_keep_awake(keepAwake)
        dh_set_language(language.rawValue)
        let refreshed = dh_settings_load()
        logMaxFileMb = Int(refreshed.logMaxFileMb)
        logCompressAfterDays = Int(refreshed.logCompressAfterDays)
        logDeleteAfterDays = Int(refreshed.logDeleteAfterDays)
    }
}
