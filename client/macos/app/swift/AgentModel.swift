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
    var rows: [AgentSourceStatus] = []

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

    func startSharing() async -> Bool {
        guard !isStarting, !isSharing else { return false }
        guard DeskhubClient.isValidPasscode(passcode) else {
            startError = DeskhubClient.string(DHStrPasscodeInvalid)
            return false
        }
        isStarting = true
        startError = ""
        save()

        let fpsNum = UInt32(max(1, fps))
        let bitrateNum = UInt32(max(1, bitrateMbps))
        let maxDimNum = maxDim <= 0 ? UInt32(0) : UInt32(maxDim)
        let portNum = UInt16(max(1, min(65535, port)))
        let allow = allowInput
        let code = acceptedPasscode

        let ok = await Task.detached {
            let picked = DeskhubAgent.listShareSources()
            guard !picked.isEmpty else { return false }
            return DeskhubAgent.start(
                sources: picked,
                fps: fpsNum,
                bitrateMbps: bitrateNum,
                maxDim: maxDimNum,
                port: portNum,
                allowInput: allow,
                passcode: code
            )
        }.value

        isStarting = false
        isSharing = ok
        if ok {
            startPolling()
        } else {
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
        rows = DeskhubAgent.status()
        if !DeskhubAgent.isRunning {
            stopSharing()
        }
    }
}
