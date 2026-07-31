import AVFoundation
import Foundation
import Observation

@MainActor @Observable
final class StreamModel {
    let address: String
    private(set) var sourceId: UInt8
    private(set) var sourceName: String

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
            endReason = "Could not connect to \(address)."
            return
        }
        failedToStart = false
        session = opened
        opened.setLayer(layer)
        startPolling()
    }

    func switchSource(to newSourceId: UInt8, name: String) async {
        guard newSourceId != sourceId else { return }
        stopPolling()
        session?.stop()
        session = nil
        sourceId = newSourceId
        sourceName = name
        endReason = ""
        statusLine = ""
        videoWidth = 0
        videoHeight = 0
        phase = .connecting
        await start()
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

    func keyTap(vk: Int32, scan: Int32) {
        session?.keyTap(vk: vk, scan: scan)
    }

    func keyChord(modVk: Int32, modScan: Int32, vk: Int32, scan: Int32) {
        session?.keyChord(modVk: modVk, modScan: modScan, vk: vk, scan: scan)
    }

    func charTap(_ codepoint: UInt32) {
        session?.charTap(codepoint)
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

    func resumePolling() {
        guard session != nil else { return }
        startPolling()
    }

    func suspendPolling() {
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
