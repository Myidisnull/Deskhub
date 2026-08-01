import AVFoundation
import SwiftUI

struct ViewerRequest: Codable, Hashable {
    var address: String
    var sourceId: UInt8
    var name: String
}

struct ViewerWindow: View {
    @State private var model: StreamModel
    @Environment(\.dismiss) private var dismiss
    @Environment(\.openWindow) private var openWindow

    init(request: ViewerRequest) {
        _model = State(initialValue: StreamModel(
            address: request.address,
            sourceId: request.sourceId,
            sourceName: request.name
        ))
    }

    var body: some View {
        StreamView(model: model) { dismiss() }
            .navigationTitle(title)
            .navigationSubtitle(subtitle)
            .frame(minWidth: 640, idealWidth: 1024, maxWidth: .infinity,
                   minHeight: 400, idealHeight: 634, maxHeight: .infinity)
            .task {
                dh_viewer_opened()
                await model.start()
            }
            .onDisappear {
                model.setLayer(nil)
                model.disconnect()
                if dh_viewer_closed() { openWindow(id: "main") }
            }
            .alert("Deskhub", isPresented: failedShown) {
                Button("OK") { dismiss() }
            } message: {
                Text(DeskhubClient.string(DHStrViewerOpenFailed))
            }
    }

    private var title: String {
        var buf = [CChar](repeating: 0, count: 320)
        _ = dh_viewer_base_title(model.sourceName, &buf, Int32(buf.count))
        return String(cString: buf)
    }

    private var subtitle: String {
        var buf = [CChar](repeating: 0, count: 320)
        let lock = DHPointerLock(locked: model.mouseLocked, paused: false)
        _ = dh_pointer_subtitle(lock, model.statusLine, &buf, Int32(buf.count))
        return String(cString: buf)
    }

    private var failedShown: Binding<Bool> {
        Binding(get: { model.failedToStart }, set: { _ in })
    }
}

struct StreamView: View {
    @Bindable var model: StreamModel
    let onEnd: () -> Void

    var body: some View {
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
        .alert("Deskhub", isPresented: endedAlertShown) {
            Button("OK") { onEnd() }
        } message: {
            Text("\(DeskhubClient.string(DHStrConnectionEndedTitle)): \(model.endReason)")
        }
    }

    private var videoSize: CGSize {
        CGSize(width: Double(model.videoWidth), height: Double(model.videoHeight))
    }

    private var endedAlertShown: Binding<Bool> {
        Binding(get: { !model.endReason.isEmpty && !model.failedToStart }, set: { _ in })
    }
}
