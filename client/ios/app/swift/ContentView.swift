import SwiftUI

struct ContentView: View {
    @State private var model = SessionModel()

    var body: some View {
        Group {
            switch model.screen {
            case .connect:
                ConnectView(model: model)
            case let .sourcePicker(sources):
                SourcePickerView(model: model, sources: sources)
            case .stream:
                if let stream = model.stream {
                    StreamView(session: model, model: stream)
                }
            case .sharing:
                EmptyView()
            }
        }
        .preferredColorScheme(.dark)
    }
}
