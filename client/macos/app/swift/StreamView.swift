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
    @Environment(ViewerRegistry.self) private var viewers

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
                viewers.count += 1
                await model.start()
            }
            .onDisappear {
                model.setLayer(nil)
                model.disconnect()
                viewers.count -= 1
                if viewers.count <= 0 { openWindow(id: "main") }
            }
            .alert("Deskhub", isPresented: failedShown) {
                Button("OK") { dismiss() }
            } message: {
                Text("Could not open a viewing session - check the address and that "
                    + "the other machine is sharing.")
            }
    }

    private var title: String {
        model.sourceName.isEmpty
            ? "Deskhub - viewing"
            : "Deskhub - viewing: \(model.sourceName)"
    }

    private var subtitle: String {
        let stats = model.statusLine.isEmpty ? "connecting..." : model.statusLine
        let hint = model.mouseLocked
            ? "Mouse locked - press F9 to release"
            : "Press F9 to lock mouse"
        return "\(stats) · \(hint)"
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
            Text("Connection ended: \(model.endReason)")
        }
    }

    private var videoSize: CGSize {
        CGSize(width: Double(model.videoWidth), height: Double(model.videoHeight))
    }

    private var endedAlertShown: Binding<Bool> {
        Binding(get: { !model.endReason.isEmpty && !model.failedToStart }, set: { _ in })
    }
}
