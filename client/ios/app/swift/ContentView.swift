import SwiftUI

struct ContentView: View {
    @State private var model = SessionModel()

    var body: some View {
        Group {
            switch model.screen {
            case .connect, .sharing:
                HomeView(model: model)
            case .connected:
                ConnectedView(model: model)
            case let .sourcePicker(sources):
                SourcePickerView(model: model, sources: sources)
            case .stream:
                if let stream = model.stream {
                    StreamView(session: model, model: stream)
                }
            }
        }
        .preferredColorScheme(.dark)
    }
}
