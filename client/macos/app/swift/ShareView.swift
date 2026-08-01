import SwiftUI

struct SharingSessionView: View {
    @Binding var route: Route
    @Bindable var model: AgentModel

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(DeskhubClient.string(DHStrSharingSourcesIntro))

            List {
                if model.rows.isEmpty {
                    Text(DeskhubClient.string(DHStrNothingShared)).foregroundStyle(.secondary)
                } else {
                    ForEach(model.rows) { row in
                        Text(row.label)
                    }
                }
            }
            .listStyle(.bordered)
            .frame(maxWidth: .infinity, maxHeight: .infinity)

            Text(DeskhubClient.string(DHStrSharingConnectHint))

            HStack {
                Spacer()
                Button(DeskhubClient.string(DHStrStopSharing)) { stop() }
            }
        }
        .padding(12)
        .onChange(of: model.isSharing) { _, sharing in
            if !sharing { route = .menu }
        }
    }

    private func stop() {
        model.stopSharing()
        route = .menu
    }
}
