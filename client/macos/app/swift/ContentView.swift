import SwiftUI

struct ContentView: View {
    var lifecycle: AppLifecycle
    @State private var route: ClientRoute = .connect
    @State private var connect = ConnectModel()
    @State private var agent = AgentModel()

    var body: some View {
        Group {
            switch route {
            case .connect, .stream, .sharing:
                MainMenuView(route: $route, connect: connect, agent: agent)
                    .frame(minWidth: 720, minHeight: 480)
            case let .sourcePicker(sources):
                SourcePickerView(route: $route, connect: connect, sources: sources)
                    .frame(width: 460, height: 340)
            }
        }
        .navigationTitle(windowTitle)
        .onAppear {
            lifecycle.attach(agent: agent)
        }
        .onChange(of: agent.runInBackground) { _, _ in
            lifecycle.applyBackgroundSetting()
        }
        .onChange(of: agent.hideTrayIcon) { _, _ in
            lifecycle.applyBackgroundSetting()
        }
    }

    private var windowTitle: String {
        switch route {
        case .sourcePicker: DeskhubClient.string(DHStrPickerTitle)
        default: DeskhubClient.string(DHStrAppTitle)
        }
    }
}
