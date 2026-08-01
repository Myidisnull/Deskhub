import AppKit
import SwiftUI

struct MainMenuView: View {
    @Binding var route: Route
    @Bindable var session: SessionModel
    @Bindable var agent: AgentModel

    @State private var shareAlert = ""
    @State private var accessibilityWarning = false
    @Environment(\.openWindow) private var openWindow
    @Environment(\.dismissWindow) private var dismissWindow

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            GroupBox("Host mode - share an application on THIS machine") {
                hostBox.padding(6)
            }
            GroupBox("Client mode - connect to ANOTHER machine") {
                clientBox.padding(6)
            }
            Button("Exit") { NSApplication.shared.terminate(nil) }
                .padding(.top, 4)
        }
        .padding(12)
        .task {
            agent.refreshPermissions()
            agent.loadAddresses()
        }
        .alert("Deskhub", isPresented: showingShareAlert) {
            if !agent.hasScreenRecording {
                Button("Open System Settings") { DeskhubAgent.openScreenRecordingSettings() }
            }
            Button("OK", role: .cancel) {}
        } message: {
            Text(shareAlert)
        }
        .alert("Deskhub", isPresented: $accessibilityWarning) {
            Button("Share anyway") { Task { await doShare() } }
            Button("Open System Settings", role: .cancel) {
                DeskhubAgent.openAccessibilitySettings()
            }
        } message: {
            Text("Mouse and keyboard are always shared, but macOS silently drops "
                + "them until Deskhub has Accessibility permission. The other "
                + "machine will see this Mac but not control it.")
        }
    }

    private var hostBox: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(DeskhubClient.string(DHStrHostIpIntro))

            if agent.addresses.isEmpty {
                Text(DeskhubClient.string(DHStrNoNetworkAddress)).foregroundStyle(.secondary)
            } else {
                ForEach(agent.addresses) { addr in
                    HStack(spacing: 8) {
                        Text(addr.name)
                            .frame(width: 150, alignment: .leading)
                            .lineLimit(1)
                        Text(addr.ip).font(.system(.body, design: .monospaced))
                            .textSelection(.enabled)
                        Spacer()
                        Button("Copy") {
                            NSPasteboard.general.clearContents()
                            NSPasteboard.general.setString(addr.ip, forType: .string)
                        }
                    }
                }
            }

            Text(DeskhubClient.string(DHStrUdpPortLine))

            HStack(spacing: 8) {
                Text("FPS").fixedSize()
                TextField("60", value: $agent.fps, format: .number)
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 52)
                Text("Bitrate (Mbps)").fixedSize()
                TextField("20", value: $agent.bitrateMbps, format: .number)
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 52)
                Text("Quality").fixedSize()
                Picker("", selection: $agent.maxDim) {
                    ForEach(DeskhubAgent.qualityPresets) { preset in
                        Text(preset.label).tag(preset.maxDim)
                    }
                }
                .labelsHidden()
                .frame(width: 96)
                Spacer(minLength: 0)
            }

            Button {
                Task { await share() }
            } label: {
                if agent.isStarting {
                    ProgressView().controlSize(.small).frame(maxWidth: .infinity)
                } else {
                    Text("Share...  (pick the display to share)")
                        .frame(maxWidth: .infinity)
                }
            }
            .disabled(agent.isStarting)
        }
    }

    private var clientBox: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(DeskhubClient.string(DHStrClientIpPrompt))
            HStack(spacing: 8) {
                TextField("", text: $session.connect.address)
                    .textFieldStyle(.roundedBorder)
                    .onSubmit(connect)
                    .disabled(session.connect.isConnecting)
                Button("Connect", action: connect)
                    .buttonStyle(.borderedProminent)
                    .disabled(session.connect.address.isEmpty || session.connect.isConnecting)
            }
            if session.connect.isConnecting {
                HStack(spacing: 8) {
                    ProgressView().controlSize(.small)
                    Text(DeskhubClient.string(DHStrQueryingSources))
                        .foregroundStyle(.secondary)
                }
            }
            if !session.connect.connectError.isEmpty {
                Text(session.connect.connectError)
                    .foregroundStyle(.red)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }

    private var showingShareAlert: Binding<Bool> {
        Binding(get: { !shareAlert.isEmpty }, set: { if !$0 { shareAlert = "" } })
    }

    private func share() async {
        agent.refreshPermissions()
        if !agent.hasScreenRecording {
            shareAlert = "Screen Recording permission is required. Grant it in "
                + "System Settings, then quit and reopen Deskhub."
            return
        }
        if !agent.hasAccessibility {
            accessibilityWarning = true
            return
        }
        await doShare()
    }

    private func doShare() async {
        if await agent.startSharing() {
            route = .sharing
        } else {
            shareAlert = agent.startError
        }
    }

    private func connect() {
        guard !session.connect.address.isEmpty, !session.connect.isConnecting else { return }
        Task {
            let sources = await session.listSources()
            if sources.count > 1 {
                route = .sourcePicker(sources)
            } else {
                openViewers(sources, address: session.connect.address,
                            openWindow: openWindow, dismissWindow: dismissWindow)
            }
        }
    }
}

@MainActor
func openViewers(_ picked: [Source], address: String,
                 openWindow: OpenWindowAction, dismissWindow: DismissWindowAction)
{
    if picked.isEmpty {
        openWindow(value: ViewerRequest(address: address, sourceId: 0, name: ""))
    } else {
        for source in picked {
            openWindow(value: ViewerRequest(
                address: address, sourceId: source.id, name: source.name
            ))
        }
    }
    dismissWindow(id: "main")
}
