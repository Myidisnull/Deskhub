import AppKit
import Foundation
import Observation

@MainActor @Observable
final class AgentModel {
    private static let stored = dh_settings_load()

    var fps = Int(AgentModel.stored.fps)
    var bitrateMbps = Int(AgentModel.stored.bitrateMbps)
    var maxDim = Int(AgentModel.stored.maxDim)
    var port = Int(AgentModel.stored.port)
    var allowInput = AgentModel.stored.allowInput
    var runInBackground = AgentModel.stored.runInBackground
    var runInBackgroundChoiceMade = AgentModel.stored.runInBackgroundChoiceMade
    var hideTrayIcon = AgentModel.stored.hideTrayIcon
    var logMaxFileMb = Int(AgentModel.stored.logMaxFileMb)
    var logCompressAfterDays = Int(AgentModel.stored.logCompressAfterDays)
    var logDeleteAfterDays = Int(AgentModel.stored.logDeleteAfterDays)
    var logDir = DeskhubClient.cString(AgentModel.stored.logDir)
    var passcode = DeskhubClient.cString(AgentModel.stored.passcode) {
        didSet {
            if DeskhubClient.isValidPasscode(passcode) { lastValidPasscode = passcode }
        }
    }

    private var lastValidPasscode = DeskhubClient.cString(AgentModel.stored.passcode)

    var addresses: [LocalAddress] = []

    var isSharing = false
    var isStarting = false
    var isStopping = false
    var startError = ""
    var clampWarning = ""
    var rows: [HostRow] = []
    var shareSources: [ShareSource] = []
    var tickedSources: Set<UInt32> = []

    var hasScreenRecording = false
    var hasAccessibility = false
    var screenRecordingNeedsRelaunch = false

    var acceptedPasscode: String {
        DeskhubClient.isValidPasscode(passcode) ? passcode : lastValidPasscode
    }

    var clientControl = AgentModel.stored.clientControl
    var bindIp = DeskhubClient.buffered(64) { dh_bind_ip($0, $1) }
    var autoShare = dh_auto_share()
    var autostart = dh_autostart_enabled()
    var clipboardSync = dh_clipboard_sync()
    var encryptSession = dh_encrypt_session()
    var escrowSessionKey = dh_escrow_session_key()
    var sessionKeyLifetime = dh_session_key_lifetime()
    var sessionKeyHex = DeskhubClient.buffered(DH_SESSION_KEY_CAP) { dh_session_key_hex($0, $1) }
    var language = AppLanguage.fromStored(DeskhubClient.buffered(32) { dh_language($0, $1) })
    var didAutoShare = false
    private var lastPasteboardChange = NSPasteboard.general.changeCount

    func applyAutostart() {
        dh_set_autostart(autostart)
        autostart = dh_autostart_enabled()
    }

    var statusLine: String {
        let portNum = UInt16(max(1, min(65535, port)))
        guard isSharing else {
            return DeskhubClient.buffered(128) { dh_idle_host_status(portNum, $0, $1) }
        }
        var line = DeskhubClient.buffered(320) {
            dh_sharing_status(portNum, acceptedPasscode, allowInput, $0, $1)
        }
        let bindWarning = DeskhubClient.buffered(256) { dha_bind_warning($0, $1) }
        if !bindWarning.isEmpty { line += " " + bindWarning }
        return line
    }

    func save() {
        if !runInBackground { hideTrayIcon = false }
        let trimmedDir = logDir.trimmingCharacters(in: .whitespacesAndNewlines)
        if !trimmedDir.isEmpty && !dh_log_dir_usable(trimmedDir) {
            logDir = DeskhubClient.cString(dh_settings_load().logDir)
            return
        }
        logDir = trimmedDir
        dh_settings_save(
            UInt32(max(1, fps)),
            UInt32(max(1, bitrateMbps)),
            maxDim <= 0 ? 0 : UInt32(maxDim),
            UInt32(max(1, port)),
            allowInput,
            clientControl,
            runInBackground,
            runInBackgroundChoiceMade,
            hideTrayIcon,
            autoShare,
            UInt32(max(1, logMaxFileMb)),
            UInt32(max(0, logCompressAfterDays)),
            UInt32(max(0, logDeleteAfterDays)),
            logDir,
            acceptedPasscode
        )
        dh_set_bind_ip(bindIp)
        dh_set_clipboard_sync(clipboardSync)
        dh_set_encrypt_session(encryptSession)
        if !encryptSession { escrowSessionKey = false }
        dh_set_escrow_session_key(escrowSessionKey)
        dh_set_session_key_lifetime(Int32(sessionKeyLifetime))
        if encryptSession {
            _ = dh_ensure_session_key(false)
            sessionKeyHex = DeskhubClient.buffered(DH_SESSION_KEY_CAP) { dh_session_key_hex($0, $1) }
        }
        dh_set_language(language.rawValue)
    }

    func refreshSessionKey() {
        guard encryptSession else { return }
        _ = dh_ensure_session_key(true)
        sessionKeyHex = DeskhubClient.buffered(DH_SESSION_KEY_CAP) { dh_session_key_hex($0, $1) }
    }

    func recordRunInBackground(_ enabled: Bool) {
        runInBackground = enabled
        runInBackgroundChoiceMade = true
        if !enabled { hideTrayIcon = false }
        save()
    }

    func recordHideTrayIcon(_ enabled: Bool) {
        hideTrayIcon = enabled
        save()
    }

    private var pollTimer: Timer?

    func refreshPermissions() {
        hasScreenRecording = DeskhubAgent.hasScreenRecording
        hasAccessibility = DeskhubAgent.hasAccessibility
        if hasScreenRecording {
            screenRecordingNeedsRelaunch = false
        }
    }

    func requestScreenRecording() {
        hasScreenRecording = DeskhubAgent.requestScreenRecording()
        screenRecordingNeedsRelaunch = !hasScreenRecording
    }

    func requestAccessibility() {
        hasAccessibility = DeskhubAgent.requestAccessibility()
    }

    func loadAddresses() {
        addresses = LocalAddress.all()
    }

    func refreshShareSources() async {
        guard !isSharing, !isStarting, !isStopping else { return }
        let found = await Task.detached { DeskhubAgent.listShareSources() }.value
        shareSources = found
        tickedSources = Set(found.map(\.id))
    }

    var pickedSources: [ShareSource] {
        shareSources.filter { tickedSources.contains($0.id) }
    }

    func runRowAction(_ row: HostRow) {
        guard isSharing else { return }
        if row.viewer {
            DeskhubAgent.kickViewer(row.sourceId, address: row.viewerAddr)
        } else {
            DeskhubAgent.stopSource(row.sourceId)
        }
    }

    func startSharing() async -> Bool {
        guard !isStarting, !isSharing, !isStopping else { return false }
        guard DeskhubClient.isValidPasscode(passcode) else {
            startError = DeskhubClient.string(DHStrPasscodeInvalid)
            return false
        }
        if shareSources.isEmpty { await refreshShareSources() }
        guard !shareSources.isEmpty else {
            startError = DeskhubClient.string(DHStrScreenRecordingRequired)
            return false
        }
        guard !pickedSources.isEmpty else {
            startError = DeskhubClient.string(DHStrNoDisplayTicked)
            return false
        }

        let picked = Array(pickedSources.prefix(DeskhubAgent.maxSources))
        clampWarning = picked.count < pickedSources.count
            ? DeskhubClient.string(DHStrShareClampWarning) : ""

        isStarting = true
        startError = ""
        save()

        let options = ShareOptions(
            fps: UInt32(max(1, fps)),
            bitrateMbps: UInt32(max(1, bitrateMbps)),
            maxDim: maxDim <= 0 ? UInt32(0) : UInt32(maxDim),
            port: UInt16(max(1, min(65535, port))),
            allowInput: allowInput,
            passcode: acceptedPasscode
        )

        let ok = await Task.detached {
            DeskhubAgent.start(sources: picked, options: options)
        }.value

        isStarting = false
        isSharing = ok
        if ok {
            startPolling()
        } else {
            clampWarning = ""
            startError = hasScreenRecording
                ? DeskhubClient.string(DHStrShareStartFailed) + ". " + DeskhubAgent.lastError
                : DeskhubClient.string(DHStrScreenRecordingRequired)
        }
        return ok
    }

    func stopSharing() {
        guard !isStopping else { return }
        isStopping = true
        stopPolling()
        Task.detached { [weak self] in
            DeskhubAgent.stop()
            await MainActor.run {
                guard let self else { return }
                self.isSharing = false
                self.isStopping = false
                self.rows = []
                Task { await self.refreshShareSources() }
            }
        }
    }

    private func startPolling() {
        stopPolling()
        pollTimer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.poll() }
        }
        poll()
    }

    private func stopPolling() {
        pollTimer?.invalidate()
        pollTimer = nil
    }

    private func poll() {
        guard !isStopping else { return }
        rows = DeskhubAgent.hostRows()
        if !DeskhubAgent.isRunning {
            stopSharing()
            return
        }
        if clipboardSync { pumpClipboard() }
    }

    private func pumpClipboard() {
        let board = NSPasteboard.general
        let remote = DeskhubClient.buffered(33000) { dha_clip_take($0, $1) }
        if !remote.isEmpty {
            board.clearContents()
            board.setString(remote, forType: .string)
            lastPasteboardChange = board.changeCount
            return
        }
        guard board.changeCount != lastPasteboardChange else { return }
        lastPasteboardChange = board.changeCount
        if let text = board.string(forType: .string), !text.isEmpty {
            dha_clip_offer(text)
        }
    }
}
