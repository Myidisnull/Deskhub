import SwiftUI

struct ContentView: View {
    @State private var route: ClientRoute = .connect
    @State private var connect = ConnectModel()
    @State private var agent = AgentModel()

    var body: some View {
        Group {
            switch route {
            case .connect, .stream, .sharing:
                MainMenuView(route: $route, connect: connect, agent: agent)
                    .frame(minWidth: 720, minHeight: 620)
            case let .sourcePicker(sources):
                SourcePickerView(route: $route, connect: connect, sources: sources)
                    .frame(width: 460, height: 340)
            }
        }
        .navigationTitle(windowTitle)
    }

    private var windowTitle: String {
        switch route {
        case .sourcePicker: DeskhubClient.string(DHStrPickerTitle)
        default: DeskhubClient.string(DHStrAppTitle)
        }
    }
}
