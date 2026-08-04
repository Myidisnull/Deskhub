import SwiftUI

struct ContentView: View {
    @State private var route: ClientRoute = .connect
    @State private var connect = ConnectModel()
    @State private var agent = AgentModel()

    var body: some View {
        Group {
            switch route {
            case .connect, .stream:
                MainMenuView(route: $route, connect: connect, agent: agent)
                    .frame(width: 500)
            case let .sourcePicker(sources):
                SourcePickerView(route: $route, connect: connect, sources: sources)
                    .frame(width: 460, height: 340)
            case .sharing:
                SharingSessionView(route: $route, model: agent)
                    .frame(width: 460, height: 330)
            }
        }
        .navigationTitle(windowTitle)
    }

    private var windowTitle: String {
        switch route {
        case .connect, .stream: DeskhubClient.string(DHStrAppTitle)
        case .sourcePicker: DeskhubClient.string(DHStrPickerTitle)
        case .sharing: DeskhubClient.string(DHStrSharingTitle)
        }
    }
}
