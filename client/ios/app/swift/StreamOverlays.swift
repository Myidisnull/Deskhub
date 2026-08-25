import SwiftUI

struct StatusOverlay: View {
    let model: StreamModel
    let streaming: Bool
    let onBack: () -> Void

    var body: some View {
        if model.phase == .ended {
            ended
        } else if model.reattaching {
            reattaching
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

    private var reattaching: some View {
        VStack(spacing: 12) {
            ProgressView()
            Text(DeskhubClient.string(DHStrLinkReattaching))
                .foregroundStyle(.white)
        }
        .allowsHitTesting(false)
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

struct StreamControlPanel: View {
    let session: SessionModel
    let model: StreamModel
    let streaming: Bool
    let hostTitle: String
    @Binding var isOpen: Bool
    @Binding var keyboardOn: Bool
    @Binding var pickingFiles: Bool
    @Binding var pickerOpen: Bool

    var body: some View {
        if isOpen {
            panel
        } else {
            openButton
        }
    }

    private var openButton: some View {
        Button {
            withAnimation(.easeOut(duration: 0.18)) { isOpen = true }
        } label: {
            Image(systemName: "slider.horizontal.3")
                .font(.system(size: 17, weight: .semibold))
                .foregroundStyle(.white)
                .frame(width: 44, height: 44)
                .background(.black.opacity(0.45), in: Circle())
                .overlay(Circle().strokeBorder(.white.opacity(0.25), lineWidth: 1))
        }
        .buttonStyle(.plain)
        .accessibilityLabel("Show controls")
    }

    private var panel: some View {
        VStack(alignment: .leading, spacing: 8) {
            header
            hotkeyStrip
            actions
        }
        .padding(12)
        .frame(maxWidth: .infinity)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 16))
        .confirmationDialog("Display", isPresented: $pickerOpen, titleVisibility: .visible) {
            ForEach(session.sources) { source in
                Button(sourceLabel(source)) { session.switchSource(to: source.id) }
            }
            Button("Cancel", role: .cancel) {}
        }
    }

    private var header: some View {
        HStack(alignment: .top, spacing: 8) {
            VStack(alignment: .leading, spacing: 2) {
                Text(hostTitle)
                    .font(.caption)
                    .foregroundStyle(.white)
                    .lineLimit(1)
                    .truncationMode(.middle)
                if streaming {
                    LinkHealthRow(health: model.linkHealth)
                        .font(.caption)
                        .foregroundStyle(.white)
                }
                if streaming, !model.statusLine.isEmpty {
                    Text(model.statusLine)
                        .font(.caption)
                        .foregroundStyle(.white.opacity(0.8))
                        .lineLimit(1)
                        .minimumScaleFactor(0.75)
                }
            }
            .frame(maxWidth: .infinity, alignment: .leading)

            Button {
                withAnimation(.easeOut(duration: 0.18)) { isOpen = false }
            } label: {
                Image(systemName: "xmark")
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundStyle(.white)
                    .frame(width: 32, height: 32)
                    .contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            .accessibilityLabel("Hide controls")
        }
    }

    private var hotkeyStrip: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: 8) {
                ForEach(kHotkeys, id: \.label) { hotkey in
                    Button(hotkey.label) { model.hotkey(hotkey) }
                        .buttonStyle(.bordered)
                }
            }
            .padding(.vertical, 1)
        }
        .disabled(!streaming)
        .opacity(streaming ? 1 : 0.45)
    }

    private var actions: some View {
        HStack(spacing: 10) {
            Button(keyboardOn ? "Hide keyboard" : "Keyboard") { keyboardOn.toggle() }
                .buttonStyle(.bordered)
                .disabled(!streaming)

            Button(DeskhubClient.string(DHStrSendFilesLabel)) { pickingFiles = true }
                .buttonStyle(.bordered)
                .disabled(!streaming)

            if session.sources.count > 1 {
                Button("Display") { pickerOpen = true }
                    .buttonStyle(.bordered)
            }

            Spacer()

            Button(DeskhubClient.string(DHStrDisconnectButton)) { session.disconnect() }
                .buttonStyle(.borderedProminent)
        }
    }

    private func sourceLabel(_ source: Source) -> String {
        let mark = source.id == model.sourceId ? "✓ " : ""
        return mark + source.pickerLabel
    }
}
