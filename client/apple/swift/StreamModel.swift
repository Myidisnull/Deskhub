import AVFoundation
import Foundation
import Observation

@MainActor @Observable
final class StreamModel {
    let address: String
    let passcode: String
    let sessionKey: String
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

    init(address: String, passcode: String, sourceId: UInt8, sourceName: String,
         sessionKey: String = "")
    {
        self.address = address
        self.passcode = passcode
        self.sessionKey = sessionKey
        self.sourceId = sourceId
        self.sourceName = sourceName
    }

    func start() async {
        let addr = address
        let sid = sourceId
        let code = passcode
        let key = sessionKey.isEmpty ? nil : sessionKey
        let handlers = makeHandlers()
        let opened = await Task.detached {
            ClientSession.start(address: addr, sourceId: sid, passcode: code, sessionKey: key,
                                handlers: handlers)
        }.value
        guard let opened else {
            failedToStart = true
            phase = .ended
            endReason = DeskhubClient.couldNotConnect(address)
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

    func offerClipboard(_ text: String) {
        session?.offerClipboard(text)
    }

    func takeClipboard() -> String? {
        session?.takeClipboard()
    }

    @discardableResult
    func sendFiles(_ paths: [String]) -> Bool {
        guard let session else { return false }
        return session.sendFiles(paths)
    }

    func fileSendError() -> String {
        session?.fileSendError() ?? ""
    }

    var aspectRatio: Double {
        guard videoWidth > 0, videoHeight > 0 else { return 16.0 / 9.0 }
        return Double(videoWidth) / Double(videoHeight)
    }

    func refresh() {
        guard let session else { return }
        let state = session.snapshot()
        phase = state.phase
        let prefix = session.linkStatusPrefix()
        if prefix.isEmpty {
            statusLine = state.statusLine
        } else if state.statusLine.isEmpty {
            statusLine = prefix
        } else {
            statusLine = prefix + " · " + state.statusLine
        }
        videoWidth = state.videoWidth
        videoHeight = state.videoHeight
        if phase == .ended {
            endReason = state.endReason
            mouseLocked = false
        }
    }

    private func makeHandlers() -> SessionHandlers {
        SessionHandlers(
            onStatus: { [weak self] line in
                Task { @MainActor in
                    guard let self else { return }
                    let prefix = self.session?.linkStatusPrefix() ?? ""
                    if prefix.isEmpty {
                        self.statusLine = line
                    } else if line.isEmpty {
                        self.statusLine = prefix
                    } else {
                        self.statusLine = prefix + " · " + line
                    }
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
