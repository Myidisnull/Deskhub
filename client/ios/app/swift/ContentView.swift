import SwiftUI

struct ContentView: View {
    @State private var model = AppModel()
    @Environment(\.scenePhase) private var scenePhase

    var body: some View {
        Group {
            switch model.screen {
            case .connect, .sharing:
                HomeView(model: model)
            case let .sourcePicker(sources):
                SourcePickerView(model: model, sources: sources)
            case .stream:
                if let stream = model.stream {
                    StreamView(session: model, model: stream)
                }
            case .terminal:
                if let terminal = model.terminal {
                    TerminalScreen(
                        model: terminal,
                        title: DeskhubClient.addressHost(model.connect.address)
                    ) { model.closeShell() }
                }
            }
        }
        .preferredColorScheme(.dark)
        .pairingPrompt(FilesHost.shared.pairing)
        .task(id: scenePhase) {
            if scenePhase == .active {
                await FilesHost.shared.run()
            } else if scenePhase == .background {
                FilesHost.shared.stop()
            }
        }
    }
}
