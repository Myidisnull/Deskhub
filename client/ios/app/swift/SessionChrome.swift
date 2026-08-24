import SwiftUI

private let closeButtonSide: CGFloat = 44
private let dimmedAlpha = 0.35

struct SessionCloseButton: View {
    let action: () -> Void
    var enabled = true

    var body: some View {
        let tint = enabled ? 1.0 : dimmedAlpha
        Button(action: action) {
            Image(systemName: "xmark")
                .font(.system(size: 17, weight: .semibold))
                .foregroundStyle(.white.opacity(tint))
                .frame(width: closeButtonSide, height: closeButtonSide)
                .background(.black.opacity(0.45), in: Circle())
                .overlay(
                    Circle().strokeBorder(.white.opacity(0.25 * tint), lineWidth: 1)
                )
        }
        .buttonStyle(.plain)
        .disabled(!enabled)
        .accessibilityLabel("Close")
    }
}
