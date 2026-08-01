import SwiftUI

struct StatusOverlay: View {
    let model: StreamModel
    let streaming: Bool
    let onBack: () -> Void

    var body: some View {
        if !model.endReason.isEmpty {
            ended
        } else if !streaming {
            connecting
        }
    }

    private var connecting: some View {
        VStack(spacing: 12) {
            ProgressView()
            Text(DeskhubClient.connectingTo(model.address))
                .foregroundStyle(.white)
        }
    }

    private var ended: some View {
        VStack(spacing: 12) {
            Text(DeskhubClient.string(DHStrSessionEnded))
                .font(.headline)
                .foregroundStyle(.white)
            Text(model.endReason)
                .foregroundStyle(.white)
                .multilineTextAlignment(.center)
                .fixedSize(horizontal: false, vertical: true)
            Button("Back", action: onBack)
                .buttonStyle(.borderedProminent)
        }
        .padding(24)
    }
}

struct ZoomControls: View {
    let zoom: CGFloat
    let panMode: Bool
    let onToggleMode: () -> Void
    let onReset: () -> Void

    var body: some View {
        VStack(alignment: .trailing, spacing: 8) {
            pill(panMode ? "Pan" : "Pointer", action: onToggleMode)
                .accessibilityLabel(
                    panMode ? "One finger pans the view" : "One finger moves the pointer"
                )
            pill(DeskhubClient.zoomLabel(Double(zoom)), action: onReset)
                .accessibilityLabel("Reset zoom")
        }
    }

    private func pill(_ text: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(text)
                .font(.caption.weight(.semibold))
                .foregroundStyle(.white)
                .padding(.horizontal, 10)
                .padding(.vertical, 6)
                .background(.black.opacity(0.45), in: Capsule())
                .overlay(Capsule().strokeBorder(.white.opacity(0.25), lineWidth: 1))
        }
        .buttonStyle(.plain)
    }
}
