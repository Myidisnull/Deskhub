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
                StreamView(model: model)
            }
        }
        .preferredColorScheme(.dark)
    }
}
