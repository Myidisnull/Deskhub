import AVFoundation
import SwiftUI
import UIKit
import UniformTypeIdentifiers

struct StreamView: View {
    @Bindable var session: SessionModel
    @Bindable var model: StreamModel
    @Environment(\.scenePhase) private var scenePhase
    @State private var layer: AVSampleBufferDisplayLayer?
    @State private var keyboardOn = false
    @State private var pickerOpen = false
    @State private var pickingFiles = false
    @State private var fileSendError = ""

    @State private var controlsOpen = false

    @State private var controlsRect: CGRect = .zero

    @State private var transform = ViewTransform()

    @State private var panMode = false

    private var streaming: Bool { model.phase == .streaming }

    var body: some View {
        GeometryReader { proxy in
            let safeArea = proxy.safeAreaInsets
            ZStack(alignment: .bottomTrailing) {
                videoArea
                    .padding(.leading, safeArea.leading)
                    .padding(.trailing, safeArea.trailing)
                StatusOverlay(model: model, streaming: streaming, onBack: session.disconnect)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .padding(safeArea)
                controlsLayer
                    .padding(safeArea)
            }
            .background(Color.black)
            .ignoresSafeArea()
        }
        .onChange(of: scenePhase) { _, newPhase in
            switch newPhase {
            case .background:
                model.releaseAllInput()
                releaseLayer()
            case .active:
                if let layer {
                    model.setLayer(layer)
                }
            case .inactive:
                break
            @unknown default:
                break
            }
        }
        .onChange(of: model.sourceId) { _, _ in
            transform = ViewTransform()
        }
        .onChange(of: transform.isZoomed) { _, zoomed in
            panMode = zoomed
        }
        .onAppear {
            UIApplication.shared.isIdleTimerDisabled = dh_keep_awake()
            model.refresh()
        }
        .task {
            guard dh_clipboard_sync() else { return }
            var lastChange = UIPasteboard.general.changeCount
            while !Task.isCancelled {
                try? await Task.sleep(for: .seconds(1))
                guard streaming, scenePhase == .active else { continue }
                let board = UIPasteboard.general
                if let remote = model.takeClipboard() {
                    board.string = remote
                    lastChange = board.changeCount
                    continue
                }
                guard board.changeCount != lastChange else { continue }
                lastChange = board.changeCount
                if board.hasStrings, let text = board.string, !text.isEmpty {
                    model.offerClipboard(text)
                }
            }
        }
        .onDisappear {
            UIApplication.shared.isIdleTimerDisabled = false
            keyboardOn = false
            releaseLayer()
        }
        .fileImporter(
            isPresented: $pickingFiles,
            allowedContentTypes: [.item],
            allowsMultipleSelection: true
        ) { result in
            switch result {
            case .success(let urls):
                let scoped = urls.map { ($0, $0.startAccessingSecurityScopedResource()) }
                defer {
                    for (url, ok) in scoped where ok {
                        url.stopAccessingSecurityScopedResource()
                    }
                }
                let paths = urls.map(\.path).filter { !$0.isEmpty }
                guard !paths.isEmpty else { return }
                if !model.sendFiles(paths) {
                    fileSendError = model.fileSendError()
                }
            case .failure(let error):
                fileSendError = error.localizedDescription
            }
        }
        .alert(DeskhubClient.string(DHStrAppTitle), isPresented: fileErrorShown) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(fileSendError)
        }
        .statusBarHidden()
    }

    private var hostTitle: String {
        DeskhubClient.hostTitle(
            model.address, width: model.videoWidth, height: model.videoHeight
        )
    }

    private var videoArea: some View {
        GeometryReader { proxy in
            let viewport = proxy.size
            let frame = transform.frame(in: viewport, aspect: aspectRatio)
            let base = ViewTransform.baseFrame(in: viewport, aspect: aspectRatio)
            ZStack(alignment: .topLeading) {
                VideoLayerView { newLayer in
                    layer = newLayer
                    model.setLayer(newLayer)
                }
                .frame(width: max(base.width, 1), height: max(base.height, 1))
                .scaleEffect(transform.zoom, anchor: .topLeading)
                .offset(x: frame.minX, y: frame.minY)

                if streaming {
                    TouchInputView(
                        model: model,
                        videoRect: frame,
                        blockedRect: controlsRect,
                        panMode: panMode,
                        zoomed: transform.isZoomed,
                        onTransform: { factor, centroid, panDelta in
                            transform.apply(
                                factor: factor, centroid: centroid, panDelta: panDelta,
                                viewport: viewport, aspect: aspectRatio
                            )
                        }
                    )
                    .frame(width: viewport.width, height: viewport.height)
                }

                KeyInputView(model: model, active: $keyboardOn)
                    .frame(width: 1, height: 1)
                    .opacity(0)
                    .allowsHitTesting(false)
            }
            .frame(width: viewport.width, height: viewport.height)
            .clipped()
        }
    }

    private var aspectRatio: CGFloat {
        CGFloat(model.aspectRatio)
    }

    private var controlsLayer: some View {
        VStack(alignment: .trailing, spacing: 8) {
            if transform.isZoomed {
                ZoomControls(
                    zoom: transform.zoom,
                    panMode: panMode,
                    onToggleMode: { panMode.toggle() },
                    onReset: {
                        withAnimation(.easeOut(duration: 0.18)) { transform = ViewTransform() }
                    }
                )
            }
            if controlsOpen {
                controlPanel
            } else {
                openButton
            }
        }
        .padding(12)
        .background(
            GeometryReader { proxy in
                Color.clear
                    .onChange(of: proxy.frame(in: .global), initial: true) { _, rect in
                        controlsRect = rect
                    }
            }
        )
    }

    private var openButton: some View {
        Button {
            withAnimation(.easeOut(duration: 0.18)) { controlsOpen = true }
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

    private var controlPanel: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(alignment: .top, spacing: 8) {
                VStack(alignment: .leading, spacing: 2) {
                    Text(hostTitle)
                        .font(.caption)
                        .foregroundStyle(.white)
                        .lineLimit(1)
                        .truncationMode(.middle)
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
                    withAnimation(.easeOut(duration: 0.18)) { controlsOpen = false }
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

                Button("End") { session.disconnect() }
                    .buttonStyle(.borderedProminent)
            }
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

    private var fileErrorShown: Binding<Bool> {
        Binding(
            get: { !fileSendError.isEmpty },
            set: { shown in
                if !shown { fileSendError = "" }
            }
        )
    }

    private func sourceLabel(_ source: Source) -> String {
        let mark = source.id == model.sourceId ? "✓ " : ""
        return mark + source.pickerLabel
    }

    private func releaseLayer() {
        model.setLayer(nil)
    }
}
