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
                        Text(label(for: row))
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

    private func label(for row: AgentSourceStatus) -> String {
        var buf = [CChar](repeating: 0, count: 320)
        _ = dh_shared_source_label(
            row.name, UInt16(clamping: row.width), UInt16(clamping: row.height),
            row.viewerConnected, &buf, Int32(buf.count)
        )
        return String(cString: buf)
    }

    private func stop() {
        model.stopSharing()
        route = .menu
    }
}
