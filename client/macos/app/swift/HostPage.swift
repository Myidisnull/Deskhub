import SwiftUI

struct HostPage: View {
    @Bindable var agent: AgentModel
    let onShare: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            deskhubHeading(DeskhubClient.string(DHStrHostHeading))
            deskhubHint(DeskhubClient.string(DHStrHostIpIntro))

            HostAddressList(addresses: agent.addresses)

            HostStatusBanner(state: shareState, detail: agent.statusLine)

            if agent.isSharing {
                HostSourceTable(rows: agent.rows) { agent.runRowAction($0) }
                    .frame(minHeight: 170)
            } else {
                SharePickerTable(sources: agent.shareSources, ticked: $agent.tickedSources)
                    .frame(minHeight: 170)
                deskhubHint(DeskhubClient.string(DHStrPickDisplaysHint))
            }

            Button {
                onShare()
            } label: {
                HStack(spacing: 8) {
                    if agent.isStarting {
                        ProgressView().controlSize(.small)
                    }
                    Text(shareState.action)
                        .font(.system(size: 15, weight: .semibold))
                }
                .frame(maxWidth: .infinity, minHeight: 26)
            }
            .buttonStyle(.borderedProminent)
            .controlSize(.large)
            .tint(agent.isSharing ? DeskhubPalette.offline : DeskhubPalette.accent)
            .disabled(agent.isStarting)
        }
        .task { await agent.refreshShareSources() }
    }

    private var shareState: HostShareState {
        if agent.isStarting { return .starting }
        return agent.isSharing ? .sharing : .idle
    }
}

enum HostShareState {
    case idle
    case starting
    case sharing

    var label: String {
        switch self {
        case .idle: DeskhubClient.string(DHStrShareStateOff)
        case .starting: DeskhubClient.string(DHStrStartingShare)
        case .sharing: DeskhubClient.string(DHStrShareStateOn)
        }
    }

    var action: String {
        switch self {
        case .idle, .starting: DeskhubClient.string(DHStrStartSharing)
        case .sharing: DeskhubClient.string(DHStrStopSharing)
        }
    }

    var tint: Color {
        switch self {
        case .idle: DeskhubPalette.muted
        case .starting: DeskhubPalette.accent
        case .sharing: DeskhubPalette.online
        }
    }
}

struct HostStatusBanner: View {
    let state: HostShareState
    let detail: String

    var body: some View {
        HStack(alignment: .top, spacing: 10) {
            Circle()
                .fill(state.tint)
                .frame(width: 10, height: 10)
                .padding(.top, 5)

            VStack(alignment: .leading, spacing: 3) {
                Text(state.label)
                    .font(.system(size: 15, weight: .bold))
                    .foregroundStyle(state.tint)
                Text(detail)
                    .foregroundStyle(DeskhubPalette.muted)
                    .fixedSize(horizontal: false, vertical: true)
            }

            Spacer(minLength: 0)
        }
        .padding(12)
        .background(
            RoundedRectangle(cornerRadius: 10).fill(state.tint.opacity(0.12))
        )
        .overlay(
            RoundedRectangle(cornerRadius: 10).stroke(state.tint.opacity(0.35), lineWidth: 1)
        )
    }
}

struct HostSourceTable: View {
    let rows: [HostRow]
    let onAction: (HostRow) -> Void

    var body: some View {
        Table(rows) {
            TableColumn("Source") { cell($0, $0.source) }.width(140)
            TableColumn("Size") { cell($0, $0.size) }.width(80)
            TableColumn("Viewers") { cell($0, $0.viewers) }.width(58)
            TableColumn("Client") { cell($0, $0.client) }.width(120)
            TableColumn("Capture") { cell($0, $0.capture) }.width(58)
            TableColumn("Send") { cell($0, $0.send) }.width(50)
            TableColumn("Mbps") { cell($0, $0.mbps) }.width(55)
            TableColumn("RTT") { cell($0, $0.rtt) }.width(55)
            TableColumn("") { action($0) }.width(104)
        }
    }

    private func cell(_ row: HostRow, _ text: String) -> some View {
        Text(text).foregroundStyle(row.online ? DeskhubPalette.online : DeskhubPalette.heading)
    }

    private func action(_ row: HostRow) -> some View {
        Button(
            DeskhubClient.string(
                row.viewer ? DHStrDisconnectViewerAction : DHStrStopDisplayAction
            )
        ) {
            onAction(row)
        }
        .buttonStyle(.borderedProminent)
        .controlSize(.small)
        .tint(row.viewer ? DeskhubPalette.warning : DeskhubPalette.offline)
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
