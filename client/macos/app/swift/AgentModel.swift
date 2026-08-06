import Foundation
import Observation

struct LocalAddress: Identifiable, Hashable {
    let ip: String
    let name: String
    var id: String { ip + name }
}

@MainActor @Observable
final class AgentModel {
    private static let stored = dh_settings_load()

    var fps = Int(AgentModel.stored.fps)
    var bitrateMbps = Int(AgentModel.stored.bitrateMbps)
    var maxDim = Int(AgentModel.stored.maxDim)
    var port = Int(AgentModel.stored.port)
    var allowInput = AgentModel.stored.allowInput
    var passcode = DeskhubClient.cString(AgentModel.stored.passcode) {
        didSet {
            if DeskhubClient.isValidPasscode(passcode) { lastValidPasscode = passcode }
        }
    }

    private var lastValidPasscode = DeskhubClient.cString(AgentModel.stored.passcode)

    var addresses: [LocalAddress] = []

    var isSharing = false
    var isStarting = false
    var startError = ""
    var clampWarning = ""
    var rows: [HostRow] = []
    var selectedRow: HostRow.ID?
    var shareSources: [ShareSource] = []
    var tickedSources: Set<UInt32> = []

    var hasScreenRecording = false
    var hasAccessibility = false
    var screenRecordingNeedsRelaunch = false

    var acceptedPasscode: String {
        DeskhubClient.isValidPasscode(passcode) ? passcode : lastValidPasscode
    }

    var clientControl = AgentModel.stored.clientControl

    var statusLine: String {
        let portNum = UInt16(max(1, min(65535, port)))
        guard isSharing else {
            return DeskhubClient.buffered(128) { dh_idle_host_status(portNum, $0, $1) }
        }
        return DeskhubClient.buffered(320) {
            dh_sharing_status(portNum, acceptedPasscode, allowInput, $0, $1)
        }
    }

    func save() {
        dh_settings_save(
            UInt32(max(1, fps)),
            UInt32(max(1, bitrateMbps)),
            maxDim <= 0 ? 0 : UInt32(maxDim),
            UInt32(max(1, port)),
            allowInput,
            clientControl,
            acceptedPasscode
        )
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
        addresses = DeskhubAgent.localAddresses()
    }

    func refreshShareSources() async {
        guard !isSharing, !isStarting else { return }
        let found = await Task.detached { DeskhubAgent.listShareSources() }.value
        shareSources = found
        tickedSources = Set(found.map(\.id))
    }

    var pickedSources: [ShareSource] {
        shareSources.filter { tickedSources.contains($0.id) }
    }

    var selectedHostRow: HostRow? {
        rows.first { $0.id == selectedRow }
    }

    var canStopDisplay: Bool {
        guard isSharing, let row = selectedHostRow else { return false }
        return !row.viewer
    }

    var canKickViewer: Bool {
        guard isSharing, let row = selectedHostRow else { return false }
        return row.viewer
    }

    func stopSelectedDisplay() {
        guard let row = selectedHostRow, !row.viewer else { return }
        DeskhubAgent.stopSource(row.sourceId)
    }

    func kickSelectedViewer() {
        guard let row = selectedHostRow, row.viewer else { return }
        DeskhubAgent.kickViewer(row.sourceId, address: row.viewerAddr)
    }

    func startSharing() async -> Bool {
        guard !isStarting, !isSharing else { return false }
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
        stopPolling()
        DeskhubAgent.stop()
        isSharing = false
        rows = []
        selectedRow = nil
        Task { await refreshShareSources() }
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
        rows = DeskhubAgent.hostRows()
        if rows.first(where: { $0.id == selectedRow }) == nil { selectedRow = nil }
        if !DeskhubAgent.isRunning {
            stopSharing()
        }
    }
}
