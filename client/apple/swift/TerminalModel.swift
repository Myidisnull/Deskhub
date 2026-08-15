import Foundation
import Observation

nonisolated enum TermKeyCode {
    static let char: Int32 = 0
    static let enter: Int32 = 1
    static let backspace: Int32 = 2
    static let tab: Int32 = 3
    static let escape: Int32 = 4
    static let up: Int32 = 5
    static let down: Int32 = 6
    static let right: Int32 = 7
    static let left: Int32 = 8
    static let home: Int32 = 9
    static let end: Int32 = 10
    static let pageUp: Int32 = 11
    static let pageDown: Int32 = 12
    static let insert: Int32 = 13
    static let delete: Int32 = 14
    static let f1: Int32 = 15
}

struct TerminalGridSnapshot {
    var rows = 0
    var cols = 0
    var cursorRow = 0
    var cursorCol = 0
    var cursorVisible = false
    var revision: UInt64 = 0
    var cells: [DHTermCell] = []

    func cell(_ row: Int, _ col: Int) -> DHTermCell {
        cells[row * cols + col]
    }
}

@MainActor @Observable
final class TerminalModel {
    static let deciding: Int32 = 2
    static let live: Int32 = 4
    static let refused: Int32 = 6

    private var handle: OpaquePointer?
    private var pollTimer: Timer?
    private var lastRevision = UInt64.max

    var state: Int32 = 0
    var message = ""
    var grid = TerminalGridSnapshot()
    var askingTrust = false
    var trustChanged = false
    var trustFingerprint = ""
    var latchCtrl = false
    var latchAlt = false

    var finished: Bool { state >= TerminalModel.refused }

    func open(address: String, passcode: String, cols: UInt16 = 100, rows: UInt16 = 30) -> Bool {
        stop()
        var callbacks = DHTermCallbacks()
        callbacks.onTrustAsked = { _, _, _ in }
        guard let opened = dh_term_open(address, passcode, cols, rows, &callbacks) else {
            return false
        }
        handle = opened
        lastRevision = .max
        startPolling()
        return true
    }

    func stop() {
        pollTimer?.invalidate()
        pollTimer = nil
        if let handle { dh_term_stop(handle) }
        handle = nil
        state = 0
    }

    func answerTrust(_ accept: Bool) {
        askingTrust = false
        guard let handle else { return }
        if accept {
            dh_term_accept_key(handle)
        } else {
            dh_term_reject_key(handle)
        }
    }

    func typeChar(_ codepoint: UInt32) {
        if latchCtrl || latchAlt {
            sendKey(TermKeyCode.char, codepoint: codepoint, alt: latchAlt, ctrl: latchCtrl)
            latchCtrl = false
            latchAlt = false
        } else if codepoint == 10 || codepoint == 13 {
            sendKey(TermKeyCode.enter)
        } else {
            sendKey(TermKeyCode.char, codepoint: codepoint)
        }
    }

    func typeSpecial(_ key: Int32) {
        sendKey(key, alt: latchAlt, ctrl: latchCtrl)
        latchCtrl = false
        latchAlt = false
    }

    func sendKey(
        _ key: Int32, codepoint: UInt32 = 0, shift: Bool = false, alt: Bool = false,
        ctrl: Bool = false
    ) {
        guard let handle else { return }
        dh_term_send_key(handle, key, codepoint, shift, alt, ctrl)
    }

    func sendText(_ text: String) {
        guard let handle, !text.isEmpty else { return }
        dh_term_send_text(handle, text)
    }

    func paste(_ text: String) {
        guard let handle, !text.isEmpty else { return }
        dh_term_paste(handle, text)
    }

    func resize(cols: Int, rows: Int) {
        guard let handle, cols > 0, rows > 0 else { return }
        dh_term_resize(handle, UInt16(clamping: cols), UInt16(clamping: rows))
    }

    private func startPolling() {
        pollTimer = Timer.scheduledTimer(withTimeInterval: 0.033, repeats: true) {
            [weak self] _ in
            Task { @MainActor in self?.poll() }
        }
    }

    private func poll() {
        guard let handle else { return }
        state = dh_term_state(handle)
        message = DeskhubClient.buffered(512) { dh_term_message(handle, $0, $1) }

        if state == TerminalModel.deciding, !askingTrust {
            trustChanged = dh_term_verdict(handle) == 2
            trustFingerprint = DeskhubClient.buffered(128) {
                dh_term_fingerprint(handle, $0, $1)
            }
            askingTrust = true
        }

        var info = DHTermGrid()
        _ = dh_term_grid(handle, 0, nil, 0, &info)
        guard info.revision != lastRevision else { return }
        let needed = Int(info.rows) * Int(info.cols)
        var cells = [DHTermCell](repeating: DHTermCell(), count: max(needed, 1))
        let filled = cells.withUnsafeMutableBufferPointer { ptr in
            dh_term_grid(handle, 0, ptr.baseAddress, UInt32(needed), &info)
        }
        guard filled || needed == 0 else { return }
        lastRevision = info.revision
        grid = TerminalGridSnapshot(
            rows: Int(info.rows),
            cols: Int(info.cols),
            cursorRow: Int(info.cursorRow),
            cursorCol: Int(info.cursorCol),
            cursorVisible: info.cursorVisible,
            revision: info.revision,
            cells: Array(cells.prefix(needed))
        )
    }
}
