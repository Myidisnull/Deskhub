import Foundation
import Observation

struct LocalAddress: Identifiable, Hashable {
    let ip: String
    let name: String
    var id: String { ip + name }
}

@MainActor @Observable
final class AgentModel {
    var fps: Int = UserDefaults.standard.object(forKey: "shareFps") as? Int ?? 60
    var bitrateMbps: Int = UserDefaults.standard.object(forKey: "shareBitrate") as? Int ?? 20
    var maxDim: Int = UserDefaults.standard.object(forKey: "shareMaxDim") as? Int ?? 1920

    var addresses: [LocalAddress] = []

    var isSharing = false
    var isStarting = false
    var startError = ""
    var rows: [AgentSourceStatus] = []

    var hasScreenRecording = false
    var hasAccessibility = false

    private var pollTimer: Timer?

    func refreshPermissions() {
        hasScreenRecording = DeskhubAgent.hasScreenRecording
        hasAccessibility = DeskhubAgent.hasAccessibility
    }

    func loadAddresses() {
        addresses = DeskhubAgent.localAddresses()
    }

    func startSharing() async -> Bool {
        guard !isStarting, !isSharing else { return false }
        isStarting = true
        startError = ""
        UserDefaults.standard.set(fps, forKey: "shareFps")
        UserDefaults.standard.set(bitrateMbps, forKey: "shareBitrate")
        UserDefaults.standard.set(maxDim, forKey: "shareMaxDim")

        let fpsNum = UInt32(max(1, fps))
        let bitrateNum = UInt32(max(1, bitrateMbps))
        let maxDimNum = maxDim <= 0 ? UInt32(0) : UInt32(max(640, maxDim))

        let ok = await Task.detached {
            let picked = DeskhubAgent.listShareSources()
            guard !picked.isEmpty else { return false }
            return DeskhubAgent.start(
                sources: picked,
                fps: fpsNum,
                bitrateMbps: bitrateNum,
                maxDim: maxDimNum
            )
        }.value

        isStarting = false
        isSharing = ok
        if ok {
            startPolling()
        } else {
            startError = hasScreenRecording
                ? "Could not start sharing. No display was found, the display may be "
                + "disconnected, or UDP port 47777 is already in use."
                : "Screen Recording permission is required. Grant it in System Settings, "
                + "then quit and reopen Deskhub."
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
