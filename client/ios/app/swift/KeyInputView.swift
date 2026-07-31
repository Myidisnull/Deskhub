import SwiftUI
import UIKit

struct KeyInputView: UIViewRepresentable {
    let model: SessionModel
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
    weak var model: SessionModel?
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
}
