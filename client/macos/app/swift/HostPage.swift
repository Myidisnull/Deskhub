import SwiftUI

struct HostPage: View {
    @Bindable var agent: AgentModel
    let onShare: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            deskhubHeading(DeskhubClient.string(DHStrHostHeading))
            deskhubHint(DeskhubClient.string(DHStrHostIpIntro))

            HostAddressList(addresses: agent.addresses)

            Text(agent.statusLine)
                .fontWeight(.bold)
                .foregroundStyle(agent.isSharing ? DeskhubPalette.online : DeskhubPalette.muted)
                .fixedSize(horizontal: false, vertical: true)

            if agent.isSharing {
                HostSourceTable(rows: agent.rows, selection: $agent.selectedRow)
                    .frame(minHeight: 170)

                HStack(spacing: 10) {
                    Button(DeskhubClient.string(DHStrStopSelectedDisplay)) {
                        agent.stopSelectedDisplay()
                    }
                    .disabled(!agent.canStopDisplay)

                    Button(DeskhubClient.string(DHStrDisconnectSelectedViewer)) {
                        agent.kickSelectedViewer()
                    }
                    .disabled(!agent.canKickViewer)
                }
            } else {
                SharePickerTable(sources: agent.shareSources, ticked: $agent.tickedSources)
                    .frame(minHeight: 170)
                deskhubHint(DeskhubClient.string(DHStrPickDisplaysHint))
            }

            Button {
                onShare()
            } label: {
                if agent.isStarting {
                    ProgressView().controlSize(.small).frame(maxWidth: .infinity)
                } else {
                    Text(DeskhubClient.string(
                        agent.isSharing ? DHStrStopSharing : DHStrShareButton
                    ))
                    .frame(maxWidth: .infinity)
                }
            }
            .frame(height: 40)
            .disabled(agent.isStarting)
        }
        .task { await agent.refreshShareSources() }
    }
}

struct HostSourceTable: View {
    let rows: [HostRow]
    @Binding var selection: HostRow.ID?

    var body: some View {
        Table(rows, selection: $selection) {
            TableColumn("Source") { cell($0, $0.source) }.width(140)
            TableColumn("Size") { cell($0, $0.size) }.width(80)
            TableColumn("Viewers") { cell($0, $0.viewers) }.width(58)
            TableColumn("Client") { cell($0, $0.client) }.width(120)
            TableColumn("Capture") { cell($0, $0.capture) }.width(58)
            TableColumn("Send") { cell($0, $0.send) }.width(50)
            TableColumn("Mbps") { cell($0, $0.mbps) }.width(55)
            TableColumn("RTT") { cell($0, $0.rtt) }.width(55)
        }
    }

    private func cell(_ row: HostRow, _ text: String) -> some View {
        Text(text).foregroundStyle(row.online ? DeskhubPalette.online : DeskhubPalette.heading)
    }
}

struct SharePickerTable: View {
    let sources: [ShareSource]
    @Binding var ticked: Set<UInt32>

    var body: some View {
        Table(sources) {
            TableColumn("") { source in
                Toggle("", isOn: tick(source)).labelsHidden()
            }
            .width(24)
            TableColumn("Source") { Text($0.name) }.width(220)
            TableColumn("Size") { Text("\($0.width)x\($0.height)") }.width(110)
        }
    }

    private func tick(_ source: ShareSource) -> Binding<Bool> {
        Binding(
            get: { ticked.contains(source.id) },
            set: { on in
                if on {
                    ticked.insert(source.id)
                } else {
                    ticked.remove(source.id)
                }
            }
        )
    }
}
