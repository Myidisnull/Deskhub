import SwiftUI

struct TerminalWindow: View {
    @State private var model: TerminalModel
    let request: TerminalRequest
    @Environment(\.dismiss) private var dismiss

    init(request: TerminalRequest) {
        self.request = request
        _model = State(initialValue: TerminalModel())
    }

    var body: some View {
        TerminalScreen(model: model, title: request.address) { closeWindow() }
            .navigationTitle(request.address)
            .onAppear { openIfNeeded() }
            .onDisappear { model.stop() }
    }

    private func openIfNeeded() {
        guard model.state == 0 else { return }
        if !model.open(address: request.address, passcode: request.passcode) {
            closeWindow()
        }
    }

    private func closeWindow() {
        model.stop()
        dismiss()
    }
}
