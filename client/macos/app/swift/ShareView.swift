import SwiftUI

struct SharingSessionView: View {
    @Binding var route: Route
    @Bindable var model: AgentModel

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Sources currently being shared:")

            List {
                if model.rows.isEmpty {
                    Text("(nothing is being shared)").foregroundStyle(.secondary)
                } else {
                    ForEach(model.rows) { row in
                        Text(label(for: row))
                    }
                }
            }
            .listStyle(.bordered)
            .frame(maxWidth: .infinity, maxHeight: .infinity)

            Text("Others connect by entering this machine's IP address.")

            HStack {
                Spacer()
                Button("Stop sharing") { stop() }
            }
        }
        .padding(12)
        .onChange(of: model.isSharing) { _, sharing in
            if !sharing { route = .menu }
        }
    }

    private func label(for row: AgentSourceStatus) -> String {
        let viewer = row.viewerConnected ? ", viewer connected" : ""
        return "\(row.name)  (\(row.width)x\(row.height)\(viewer))"
    }

    private func stop() {
        model.stopSharing()
        route = .menu
    }
}
