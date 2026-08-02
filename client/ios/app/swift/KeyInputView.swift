import SwiftUI
import UIKit

struct KeyInputView: UIViewRepresentable {
    let model: StreamModel
    @Binding var active: Bool

    func makeUIView(context _: Context) -> KeyCaptureUIView {
        let view = KeyCaptureUIView()
        view.model = model
        return view
    }

    func updateUIView(_ uiView: KeyCaptureUIView, context _: Context) {
        uiView.model = model
        let binding = $active
        uiView.onKeyboardDismissed = { binding.wrappedValue = false }
        if active, !uiView.isFirstResponder {
            uiView.becomeFirstResponder()
        } else if !active, uiView.isFirstResponder {
            uiView.resignFirstResponder()
        }
    }
}

final class KeyCaptureUIView: UIView, UIKeyInput {
    weak var model: StreamModel?
    var onKeyboardDismissed: (() -> Void)?

    override var inputAccessoryView: UIView? { accessoryBar }

    private lazy var accessoryBar: UIView = {
        let bar = UIView(frame: CGRect(x: 0, y: 0, width: 0, height: 48))
        var config = UIButton.Configuration.gray()
        config.title = "Done"
        config.buttonSize = .mini
        let done = UIButton(
            configuration: config,
            primaryAction: UIAction { [weak self] _ in
                guard let self else { return }
                onKeyboardDismissed?()
                resignFirstResponder()
            }
        )
        done.translatesAutoresizingMaskIntoConstraints = false
        bar.addSubview(done)
        NSLayoutConstraint.activate([
            done.trailingAnchor.constraint(equalTo: bar.trailingAnchor, constant: -8),
            done.topAnchor.constraint(equalTo: bar.topAnchor, constant: 4),
        ])
        return bar
    }()

    var keyboardType: UIKeyboardType = .asciiCapable
    var autocorrectionType: UITextAutocorrectionType = .no
    var spellCheckingType: UITextSpellCheckingType = .no
    var autocapitalizationType: UITextAutocapitalizationType = .none

    override init(frame: CGRect) {
        super.init(frame: frame)
        NotificationCenter.default.addObserver(
            self, selector: #selector(keyboardWillHide),
            name: UIResponder.keyboardWillHideNotification, object: nil
        )
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) { fatalError("init(coder:) is not supported") }

    @objc private func keyboardWillHide() {
        if isFirstResponder { onKeyboardDismissed?() }
    }

    override var canBecomeFirstResponder: Bool { true }

    var hasText: Bool { true }

    func insertText(_ text: String) {
        for scalar in text.unicodeScalars {
            model?.charTap(scalar.value)
        }
    }

    func deleteBackward() {
        model?.charTap(0x08)
    }

    private static let hidToVk: [UIKeyboardHIDUsage: Int32] = [
        .keyboardEscape: 0x1B,
        .keyboardLeftArrow: 0x25, .keyboardUpArrow: 0x26,
        .keyboardRightArrow: 0x27, .keyboardDownArrow: 0x28,
        .keyboardHome: 0x24, .keyboardEnd: 0x23,
        .keyboardPageUp: 0x21, .keyboardPageDown: 0x22,
        .keyboardInsert: 0x2D, .keyboardDeleteForward: 0x2E,
        .keyboardF1: 0x70, .keyboardF2: 0x71, .keyboardF3: 0x72, .keyboardF4: 0x73,
        .keyboardF5: 0x74, .keyboardF6: 0x75, .keyboardF7: 0x76, .keyboardF8: 0x77,
        .keyboardF9: 0x78, .keyboardF10: 0x79, .keyboardF11: 0x7A, .keyboardF12: 0x7B,
        .keyboardLeftShift: 0xA0, .keyboardRightShift: 0xA1,
        .keyboardLeftControl: 0xA2, .keyboardRightControl: 0xA3,
        .keyboardLeftAlt: 0xA4, .keyboardRightAlt: 0xA5,
        .keyboardLeftGUI: 0x5B, .keyboardRightGUI: 0x5C,
    ]

    override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        let unhandled = handlePresses(presses, down: true)
        if !unhandled.isEmpty { super.pressesBegan(unhandled, with: event) }
    }

    override func pressesEnded(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        let unhandled = handlePresses(presses, down: false)
        if !unhandled.isEmpty { super.pressesEnded(unhandled, with: event) }
    }

    override func pressesCancelled(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
        let unhandled = handlePresses(presses, down: false)
        if !unhandled.isEmpty { super.pressesCancelled(unhandled, with: event) }
    }

    private func handlePresses(_ presses: Set<UIPress>, down: Bool) -> Set<UIPress> {
        var unhandled = Set<UIPress>()
        for press in presses {
            guard let key = press.key else {
                unhandled.insert(press)
                continue
            }
            if let vk = Self.hidToVk[key.keyCode] {
                model?.key(vk: vk, scan: dh_vk_scancode(vk), down: down)
                continue
            }
            if down, let scalar = chordScalar(for: key) {
                model?.charTap(scalar.value)
                continue
            }
            unhandled.insert(press)
        }
        return unhandled
    }

    private func chordScalar(for key: UIKey) -> Unicode.Scalar? {
        let held: UIKeyModifierFlags = [.control, .command, .alternate]
        guard !key.modifierFlags.isDisjoint(with: held) else { return nil }
        guard let scalar = key.charactersIgnoringModifiers.unicodeScalars.first,
              scalar.value >= 0x20 else { return nil }
        return scalar
    }
}
