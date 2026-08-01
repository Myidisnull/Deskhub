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

    init(address: String, sourceId: UInt8, sourceName: String) {
        self.address = address
        self.sourceId = sourceId
        self.sourceName = sourceName
    }

    func start() async {
        let addr = address
        let sid = sourceId
        let handlers = makeHandlers()
        let opened = await Task.detached {
            ClientSession.start(address: addr, sourceId: sid, handlers: handlers)
        }.value
        guard let opened else {
            failedToStart = true
            phase = .ended
            endReason = "Could not connect to \(address)."
            return
        }
        failedToStart = false
        session = opened
        opened.setLayer(layer)
        refresh()
    }

    func switchSource(to newSourceId: UInt8, name: String) async {
        guard newSourceId != sourceId else { return }
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

    func hotkey(_ hotkey: Hotkey) {
        session?.hotkey(hotkey)
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

    func mouseWheelNotches(_ notches: Int32) {
        session?.mouseWheelNotches(notches)
    }

    func mouseWheel(_ delta: Int32) {
        session?.mouseWheel(delta)
    }

    func refresh() {
        guard let session else { return }
        phase = session.phase()
        statusLine = session.statusLine()
        videoWidth = session.videoWidth()
        videoHeight = session.videoHeight()
        if phase == .ended {
            endReason = session.endReason()
            mouseLocked = false
        }
    }

    private func makeHandlers() -> SessionHandlers {
        SessionHandlers(
            onStatus: { [weak self] line in
                Task { @MainActor in
                    guard let self else { return }
                    self.statusLine = line
                    self.phase = self.session?.phase() ?? self.phase
                }
            },
            onSize: { [weak self] width, height in
                Task { @MainActor in
                    guard let self else { return }
                    self.videoWidth = width
                    self.videoHeight = height
                    self.phase = self.session?.phase() ?? self.phase
                }
            },
            onClosed: { [weak self] reason in
                Task { @MainActor in
                    guard let self else { return }
                    self.phase = .ended
                    self.endReason = reason
                    self.mouseLocked = false
                }
            }
        )
    }
}
