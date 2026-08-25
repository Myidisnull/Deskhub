import AppKit
import AVFoundation
import SwiftUI
import UniformTypeIdentifiers

struct ViewerRequest: Codable, Hashable {
    var address: String
    var passcode: String
    var sessionKey: String = ""
    var sourceId: UInt8
    var name: String
    var openFiles: Bool = false
}

struct ViewerWindow: View {
    @State private var model: StreamModel
    @State private var pickingFiles = false
    @State private var fileSendError = ""
    @State private var openFilesOnStream: Bool
    @Environment(\.dismiss) private var dismiss
    @Environment(\.openWindow) private var openWindow

    init(request: ViewerRequest) {
        _model = State(initialValue: StreamModel(
            address: request.address,
            passcode: request.passcode,
            sourceId: request.sourceId,
            sourceName: request.name,
            sessionKey: request.sessionKey
        ))
        _openFilesOnStream = State(initialValue: request.openFiles)
    }

    var body: some View {
        StreamView(model: model) { closeWindow() }
            .navigationTitle(title)
            .navigationSubtitle(subtitle)
            .toolbar {
                ToolbarItem(placement: .primaryAction) {
                    Button(DeskhubClient.string(DHStrSendFilesLabel)) {
                        pickingFiles = true
                    }
                    .disabled(model.phase != .streaming)
                }
            }
            .fileImporter(
                isPresented: $pickingFiles,
                allowedContentTypes: [.item],
                allowsMultipleSelection: true
            ) { result in
                switch result {
                case let .success(urls):
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
                case let .failure(error):
                    fileSendError = error.localizedDescription
                }
            }
            .frame(minWidth: 640, idealWidth: 1024, maxWidth: .infinity,
                   minHeight: 400, idealHeight: 634, maxHeight: .infinity)
            .task {
                dh_viewer_opened()
                await model.start()
            }
            .onChange(of: model.phase) { _, phase in
                guard phase == .streaming, openFilesOnStream else { return }
                openFilesOnStream = false
                pickingFiles = true
            }
            .task {
                guard dh_clipboard_sync() else { return }
                var lastChange = NSPasteboard.general.changeCount
                while !Task.isCancelled {
                    try? await Task.sleep(for: .seconds(1))
                    let board = NSPasteboard.general
                    if let remote = model.takeClipboard() {
                        board.clearContents()
                        board.setString(remote, forType: .string)
                        lastChange = board.changeCount
                        continue
                    }
                    guard board.changeCount != lastChange else { continue }
                    lastChange = board.changeCount
                    if let text = board.string(forType: .string), !text.isEmpty {
                        model.offerClipboard(text)
                    }
                }
            }
            .onDisappear {
                model.setLayer(nil)
                model.disconnect()
                if dh_viewer_closed() { openWindow(id: "main") }
            }
            .alert(DeskhubClient.string(DHStrAppTitle), isPresented: failedShown) {
                Button("OK") { closeWindow() }
            } message: {
                Text(DeskhubClient.string(DHStrViewerOpenFailed))
            }
            .alert(DeskhubClient.string(DHStrAppTitle), isPresented: fileErrorShown) {
                Button("OK", role: .cancel) {}
            } message: {
                Text(fileSendError)
            }
    }

    private func closeWindow() {
        DispatchQueue.main.async { dismiss() }
    }

    private var title: String {
        DeskhubClient.viewerBaseTitle(model.sourceName)
    }

    private var subtitle: String {
        DeskhubClient.pointerSubtitle(locked: model.mouseLocked, statusLine: model.statusLine)
    }

    private var failedShown: Binding<Bool> {
        Binding(
            get: { model.failedToStart },
            set: { shown in
                if !shown {
                    model.failedToStart = false
                    model.endReason = ""
                }
            }
        )
    }

    private var fileErrorShown: Binding<Bool> {
        Binding(
            get: { !fileSendError.isEmpty },
            set: { shown in
                if !shown { fileSendError = "" }
            }
        )
    }
}

struct StreamView: View {
    @Bindable var model: StreamModel
    let onEnd: () -> Void

    var body: some View {
        VStack(spacing: 0) {
            ConnectionStatusBar(model: model, onDisconnect: onEnd)
            RemoteView(
                model: model,
                videoSize: videoSize,
                mouseLocked: model.mouseLocked,
                onLayerReady: { layer in model.setLayer(layer) },
                onLockChanged: { model.mouseLocked = $0 }
            )
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(Color.black)
            .environment(\.colorScheme, .dark)
        }
        .alert(DeskhubClient.string(DHStrAppTitle), isPresented: endedAlertShown) {
            Button("OK") { onEnd() }
        } message: {
            Text("\(DeskhubClient.string(DHStrConnectionEndedTitle)): \(model.endReason)")
        }
    }

    private var videoSize: CGSize {
        CGSize(width: Double(model.videoWidth), height: Double(model.videoHeight))
    }

    private var endedAlertShown: Binding<Bool> {
        Binding(
            get: { !model.endReason.isEmpty && !model.failedToStart },
            set: { shown in
                if !shown {
                    model.endReason = ""
                }
            }
        )
    }
}
