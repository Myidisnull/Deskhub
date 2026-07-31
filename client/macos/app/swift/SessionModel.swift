import AVFoundation
import Foundation
import Observation

@MainActor @Observable
final class SessionModel {
    var address: String = UserDefaults.standard.string(forKey: "lastAddress") ?? ""
    var isConnecting = false
    var connectError = ""

    func listSources() async -> [Source] {
        guard !address.isEmpty else { return [] }
        isConnecting = true
        connectError = ""
        let addr = address
        UserDefaults.standard.set(addr, forKey: "lastAddress")

        let found = await Task.detached { DeskhubClient.listSources(address: addr) }.value
        isConnecting = false
        return found
    }
}

@MainActor @Observable
final class StreamModel {
    let address: String
    let sourceId: UInt8
    let sourceName: String

    var phase: Phase = .connecting
    var statusLine = ""
    var endReason = ""
    var videoWidth: UInt32 = 0
    var videoHeight: UInt32 = 0
    var failedToStart = false

    var mouseLocked = false

    private var session: ClientSession?
    private var layer: AVSampleBufferDisplayLayer?
    private var pollTimer: Timer?

    init(address: String, sourceId: UInt8, sourceName: String) {
        self.address = address
        self.sourceId = sourceId
        self.sourceName = sourceName
    }

    func start() async {
        let addr = address
        let sid = sourceId
        let opened = await Task.detached { ClientSession.start(address: addr, sourceId: sid) }.value
        guard let opened else {
            failedToStart = true
            phase = .ended
            return
        }
        session = opened
        opened.setLayer(layer)
        startPolling()
    }

    func disconnect() {
        stopPolling()
        mouseLocked = false
        session?.stop()
        session = nil
        phase = .idle
        statusLine = ""
    }

    func setLayer(_ newLayer: AVSampleBufferDisplayLayer?) {
        layer = newLayer
        session?.setLayer(newLayer)
    }

    func key(vk: Int32, scan: Int32, down: Bool) {
        session?.key(vk: vk, scan: scan, down: down)
    }

    func releaseAllInput() {
        session?.releaseAllInput()
    }

    func mouseMove(nx: Int32, ny: Int32) {
        session?.mouseMove(nx: nx, ny: ny)
    }

    func mouseMoveRel(dx: Int32, dy: Int32) {
        session?.mouseMoveRel(dx: dx, dy: dy)
    }

    func mouseButton(_ button: MouseButton, down: Bool) {
        session?.mouseButton(button, down: down)
    }

    func mouseWheel(_ delta: Int32) {
        session?.mouseWheel(delta)
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
        guard let session else { return }
        phase = session.phase()
        statusLine = session.statusLine()
        videoWidth = session.videoWidth()
        videoHeight = session.videoHeight()

        if phase == .ended {
            endReason = session.endReason()
            mouseLocked = false
            stopPolling()
        }
    }
}
