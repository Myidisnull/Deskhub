import Foundation

@MainActor
final class LocalShellFeed: TerminalFeed {
    private let termId: UInt32
    private var closed = false

    init(termId: UInt32) {
        self.termId = termId
    }

    private var alive: Bool {
        !closed && dha_local_shell_alive(termId)
    }

    var state: Int32 {
        alive ? TerminalModel.live : TerminalModel.ended
    }

    var message: String {
        DeskhubClient.string(alive ? DHStrTerminalAttachedHere : DHStrTerminalClosed)
    }

    var trustVerdict: Int32 { 0 }
    var fingerprint: String { "" }

    func answerTrust(_: Bool) {}

    func grid(
        into cells: UnsafeMutablePointer<DHTermCell>?, capacity: UInt32, info: inout DHTermGrid
    ) -> Bool {
        dha_local_grid(termId, 0, cells, capacity, &info)
    }

    func sendKey(_ key: Int32, codepoint: UInt32, shift: Bool, alt: Bool, ctrl: Bool) {
        dha_local_send_key(termId, key, codepoint, shift, alt, ctrl)
    }

    func sendText(_ text: String) {
        dha_local_send_text(termId, text)
    }

    func paste(_ text: String) {
        sendText(text)
    }

    func resize(cols: UInt16, rows: UInt16) {
        dha_local_resize(termId, cols, rows)
    }

    func stop() {
        guard !closed else { return }
        closed = true
        dha_close_local_shell(termId)
    }
}
