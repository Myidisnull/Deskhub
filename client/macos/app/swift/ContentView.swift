import SwiftUI

struct ContentView: View {
    var sharing: SharingModel
    @State private var route: ClientRoute = .connect
    @State private var connect = ConnectModel()

    var body: some View {
        Group {
            switch route {
            case .connect, .stream, .terminal, .sharing:
                MainMenuView(route: $route, connect: connect, sharing: sharing)
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
