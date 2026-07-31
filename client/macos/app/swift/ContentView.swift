import SwiftUI

enum Route {
    case menu
    case sourcePicker([Source])
    case sharing
}

struct ContentView: View {
    @State private var route: Route = .menu
    @State private var session = SessionModel()
    @State private var agent = AgentModel()

    var body: some View {
        Group {
            switch route {
            case .menu:
                MainMenuView(route: $route, session: session, agent: agent)
                    .frame(width: 500)
            case let .sourcePicker(sources):
                SourcePickerView(route: $route, model: session, sources: sources)
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
        case .menu: "Deskhub - stream & remotely control an application"
        case .sourcePicker: "What do you want to view?"
        case .sharing: "Deskhub - sharing"
        }
    }
}
