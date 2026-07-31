import Foundation
import Observation

enum AppScreen: Sendable {
    case connect
    case sourcePicker([Source])
    case stream
}

@MainActor @Observable
final class SessionModel {
    var screen: AppScreen = .connect
    var address: String = UserDefaults.standard.string(forKey: "lastAddress") ?? ""
    var isConnecting = false
    var connectError = ""
    var phase: Phase = .idle
    var statusLine = ""
    var endReason = ""
    var videoWidth: UInt32 = 0
    var videoHeight: UInt32 = 0

    var sources: [Source] = []
    var currentSourceId: UInt8 = 0

    private var pollTimer: Timer?

    func connect() {
        let addr = address.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !addr.isEmpty, !isConnecting else { return }
        address = addr
        isConnecting = true
        connectError = ""
        UserDefaults.standard.set(addr, forKey: "lastAddress")

        Task {
            let found = await Task.detached { DeskhubClient.listSources(address: addr) }.value
            isConnecting = false
            sources = found
            if found.count > 1 {
                screen = .sourcePicker(found)
            } else {
                startStream(sourceId: found.first?.id ?? 0)
            }
        }
    }

    func startStream(sourceId: UInt8) {
        endReason = ""
        statusLine = ""
        connectError = ""
        phase = .connecting
        currentSourceId = sourceId
        guard DeskhubClient.start(address: address, sourceId: sourceId) else {
            phase = .idle
            connectError = "Could not connect to \(address). Enter just the IP address."
            screen = .connect
            return
        }
        screen = .stream
        startPolling()
    }

    func switchSource(to sourceId: UInt8) {
        guard sourceId != currentSourceId else { return }
        stopPolling()
        DeskhubClient.stop()
        endReason = ""
        statusLine = ""
        videoWidth = 0
        videoHeight = 0
        phase = .connecting
        currentSourceId = sourceId
        guard DeskhubClient.start(address: address, sourceId: sourceId) else {
            phase = .idle
            endReason = "Could not switch display."
            return
        }
        startPolling()
    }

    func disconnect() {
        stopPolling()
        DeskhubClient.stop()
        phase = .idle
        statusLine = ""
        screen = .connect
    }

    func keyTap(vk: Int32, scan: Int32) {
        DeskhubClient.keyTap(vk: vk, scan: scan)
    }

    func keyChord(modVk: Int32, modScan: Int32, vk: Int32, scan: Int32) {
        DeskhubClient.keyChord(modVk: modVk, modScan: modScan, vk: vk, scan: scan)
    }

    func mouseMove(nx: Int32, ny: Int32) {
        DeskhubClient.mouseMove(nx: nx, ny: ny)
    }

    func mouseButton(_ button: MouseButton, down: Bool) {
        DeskhubClient.mouseButton(button, down: down)
    }

    func charTap(_ codepoint: UInt32) {
        DeskhubClient.charTap(codepoint)
    }

    func streamViewAppeared() {
        startPolling()
    }

    func streamViewDisappeared() {
        stopPolling()
    }

    private func startPolling() {
        stopPolling()
        let timer = Timer(timeInterval: 0.5, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.poll() }
        }
        RunLoop.main.add(timer, forMode: .common)
        pollTimer = timer
        poll()
    }

    private func stopPolling() {
        pollTimer?.invalidate()
        pollTimer = nil
    }

    private func poll() {
        phase = DeskhubClient.phase()
        statusLine = DeskhubClient.statusLine()
        videoWidth = DeskhubClient.videoWidth()
        videoHeight = DeskhubClient.videoHeight()

        if phase == .ended {
            endReason = DeskhubClient.endReason()
            stopPolling()
        }
    }
}
