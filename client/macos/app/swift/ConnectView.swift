import AppKit
import SwiftUI

enum DeskhubPage: Int, CaseIterable, Identifiable {
    case host
    case client
    case settings

    var id: Int { rawValue }

    var label: String {
        switch self {
        case .host: DeskhubClient.string(DHStrSidebarHost)
        case .client: DeskhubClient.string(DHStrSidebarClient)
        case .settings: DeskhubClient.string(DHStrSidebarSettings)
        }
    }
}

struct MainMenuView: View {
    private static let portSettle = Duration.milliseconds(600)

    @Binding var route: ClientRoute
    @Bindable var connect: ConnectModel
    @Bindable var agent: AgentModel

    @State private var discovery = DiscoveryModel()
    @State private var page: DeskhubPage = .client
    @State private var shareAlert = ""
    @State private var accessibilityWarning = false
    @State private var prompting: DeviceListRow?
    @State private var promptPasscode = ""
    @State private var promptPort = ""
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        HStack(spacing: 0) {
            sidebar
            Divider()
            ScrollView {
                page(for: page)
                    .padding(16)
                    .frame(maxWidth: .infinity, alignment: .topLeading)
            }
            .frame(minWidth: 520, minHeight: 400)
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(Color(nsColor: .textBackgroundColor))
        }
        .task {
            agent.refreshPermissions()
            agent.loadAddresses()
            discovery.start()
            if agent.autoShare, !agent.didAutoShare, !agent.isSharing, !agent.isStarting {
                agent.didAutoShare = true
                page = .host
                await share()
            }
        }
        .task(id: agent.port) {
            try? await Task.sleep(for: MainMenuView.portSettle)
            guard !Task.isCancelled, agent.port >= 1, agent.port <= 65535 else { return }
            discovery.usePort(UInt16(agent.port))
        }
        .alert(DeskhubClient.string(DHStrAppTitle), isPresented: showingShareAlert) {
            if !agent.hasScreenRecording {
                Button("Grant Screen Recording") { agent.requestScreenRecording() }
            }
            Button("OK", role: .cancel) {}
        } message: {
            Text(shareAlert)
        }
        .sheet(item: $prompting) { row in
            PasscodePromptSheet(
                address: row.addr,
                port: $promptPort,
                passcode: $promptPasscode,
                onCancel: { prompting = nil },
                onConnect: { confirmPrompt(row) }
            )
        }
        .alert(DeskhubClient.string(DHStrAppTitle), isPresented: $accessibilityWarning) {
            Button("Share anyway") { Task { await doShare() } }
            Button("Grant Accessibility", role: .cancel) {
                agent.requestAccessibility()
            }
        } message: {
            Text("Mouse and keyboard are always shared, but macOS silently drops "
                + "them until \(DeskhubClient.string(DHStrAppTitle)) has Accessibility permission. The other "
                + "machine will see this Mac but not control it.")
        }
    }

    private var sidebar: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text(DeskhubClient.string(DHStrAppTitle))
                .font(.system(size: 26, weight: .bold))
                .foregroundStyle(.white)
                .padding(16)

            ForEach(DeskhubPage.allCases) { item in
                Button {
                    page = item
                } label: {
                    Text(item.label)
                        .font(.system(size: 14, weight: page == item ? .bold : .regular))
                        .foregroundStyle(page == item ? Color.white : DeskhubPalette.navText)
                        .frame(maxWidth: .infinity, minHeight: 42, alignment: .leading)
                        .padding(.horizontal, 16)
                        .background(
                            RoundedRectangle(cornerRadius: 8)
                                .fill(page == item ? DeskhubPalette.accent : Color.clear)
                        )
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .padding(.horizontal, 10)
                .padding(.bottom, 10)
            }

            Spacer(minLength: 0)

            if let url = URL(string: DeskhubClient.string(DHStrProjectUrl)) {
                Link(DeskhubClient.string(DHStrProjectLinkLabel), destination: url)
                    .foregroundStyle(DeskhubPalette.navText)
                    .padding(.horizontal, 16)
            }

            Text(DeskhubClient.buffered(64) { dh_version_line($0, $1) })
                .font(.caption)
                .foregroundStyle(DeskhubPalette.footnote)
                .padding(16)
        }
        .frame(width: 180)
        .frame(maxHeight: .infinity)
        .background(DeskhubPalette.sidebar)
    }

    @ViewBuilder
    private func page(for page: DeskhubPage) -> some View {
        switch page {
        case .host: HostPage(agent: agent) { Task { await share() } }
        case .client: clientPage
        case .settings: SettingsPage(agent: agent)
        }
    }

    private var clientPage: some View {
        VStack(alignment: .leading, spacing: 10) {
            deskhubHeading(DeskhubClient.string(DHStrClientHeading))

            Grid(alignment: .leading, horizontalSpacing: 12, verticalSpacing: 12) {
                GridRow {
                    Text(DeskhubClient.string(DHStrClientIpPrompt))
                    TextField(
                        DeskhubClient.string(DHStrClientIpPlaceholder), text: $connect.address
                    )
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 260)
                    .onSubmit(beginConnect)
                    .disabled(connect.isConnecting)
                }
                GridRow {
                    Text(DeskhubClient.string(DHStrUdpPortLabel))
                    TextField("", text: $connect.port)
                        .textFieldStyle(.roundedBorder)
                        .frame(width: 260)
                        .onSubmit(beginConnect)
                        .disabled(connect.isConnecting)
                }
                GridRow {
                    Text(DeskhubClient.string(DHStrClientPasscodePrompt))
                    PasscodeField(
                        passcode: $connect.passcode,
                        width: 260,
                        enabled: !connect.isConnecting,
                        onSubmit: beginConnect
                    )
                }
                GridRow {
                    Text(DeskhubClient.string(DHStrClientSessionKeyPrompt))
                    TextField("", text: $connect.sessionKey)
                        .textFieldStyle(.roundedBorder)
                        .frame(width: 260)
                        .help(DeskhubClient.string(DHStrClientSessionKeyHint))
                        .onSubmit(beginConnect)
                        .disabled(connect.isConnecting)
                }
                GridRow {
                    Text("Your name")
                    TextField("", text: $connect.deviceName)
                        .textFieldStyle(.roundedBorder)
                        .frame(width: 260)
                        .onSubmit(beginConnect)
                        .disabled(connect.isConnecting)
                }
            }

            deskhubHint(DeskhubClient.string(DHStrClientPasscodeHint))
            deskhubHint(DeskhubClient.string(DHStrClientSessionKeyHint))

            Button(action: beginConnect) {
                Text("Connect").deskhubPrimaryLabel()
            }
            .buttonStyle(.borderedProminent)
            .controlSize(.large)
            .tint(DeskhubPalette.accent)
            .disabled(connect.address.isEmpty || connect.isConnecting)

            Toggle(DeskhubClient.string(DHStrRequestControlLabel), isOn: $agent.clientControl)
                .onChange(of: agent.clientControl) { _, _ in agent.save() }

            if connect.isConnecting {
                HStack(spacing: 8) {
                    ProgressView().controlSize(.small)
                    Text(DeskhubClient.string(DHStrQueryingSources)).foregroundStyle(.secondary)
                }
            }

            if !connect.connectError.isEmpty {
                Text(connect.connectError)
                    .foregroundStyle(.red)
                    .fixedSize(horizontal: false, vertical: true)
            }

            deskhubHeadingRow(DeskhubClient.string(DHStrLanDevicesHeading)) {
                discovery.rescanNow()
            }
            DeviceTable(
                rows: discovery.scanHits.map {
                    DeviceListRow($0, passcode: DeskhubDiscovery.passcode(for: $0.addr))
                },
                note: discovery.scanStatus,
                withHistory: false,
                enabled: !connect.isConnecting,
                onPick: pick
            )

            deskhubHeadingRow(DeskhubClient.string(DHStrRecentDevicesHeading)) {
                discovery.refreshStatus()
            }
            DeviceTable(
                rows: discovery.recent.map { DeviceListRow($0) },
                note: discovery.recentNote,
                withHistory: true,
                enabled: !connect.isConnecting,
                onPick: pick,
                onForget: { row in
                    Task { await discovery.forget(address: row.addr) }
                }
            )
        }
    }

    private var showingShareAlert: Binding<Bool> {
        Binding(get: { !shareAlert.isEmpty }, set: { if !$0 { shareAlert = "" } })
    }

    private func share() async {
        if agent.isSharing {
            agent.stopSharing()
            return
        }
        agent.refreshPermissions()
        if !agent.hasScreenRecording {
            shareAlert = DeskhubClient.string(DHStrScreenRecordingRequired)
            return
        }
        if !agent.hasAccessibility {
            accessibilityWarning = true
            return
        }
        await doShare()
    }

    private func doShare() async {
        guard await agent.startSharing() else {
            shareAlert = agent.startError
            return
        }
        if !agent.clampWarning.isEmpty {
            shareAlert = agent.clampWarning
            agent.clampWarning = ""
        }
    }

    private func pick(_ row: DeviceListRow) {
        promptPasscode = DeskhubClient.isValidPasscode(row.passcode)
            ? row.passcode : connect.passcode
        promptPort = DeskhubClient.addressPortText(row.addr)
        prompting = row
    }

    private func confirmPrompt(_ row: DeviceListRow) {
        prompting = nil
        connect.address = DeskhubClient.addressHost(row.addr)
        connect.port = promptPort
        connect.passcode = promptPasscode
        beginConnect()
    }

    private func beginConnect() {
        guard !connect.address.isEmpty, !connect.isConnecting else { return }
        connect.saveDeviceName()
        Task {
            let sources = await connect.listSources()
            guard !connect.acceptedAddress.isEmpty, connect.connectError.isEmpty else { return }
            await discovery.remember(
                address: connect.acceptedAddress, passcode: connect.acceptedPasscode,
                sessionKey: connect.acceptedSessionKey
            )
            if DeskhubClient.connectDecision(sources).showPicker {
                route = .sourcePicker(sources)
            } else {
                openViewers(sources, address: connect.acceptedAddress,
                            passcode: connect.acceptedPasscode,
                            sessionKey: connect.acceptedSessionKey, openWindow: openWindow)
            }
        }
    }
}

@MainActor
func openViewers(_ picked: [Source], address: String, passcode: String,
                 sessionKey: String = "", openWindow: OpenWindowAction)
{
    if picked.isEmpty {
        openWindow(value: ViewerRequest(
            address: address, passcode: passcode, sessionKey: sessionKey, sourceId: 0, name: ""
        ))
    } else {
        for source in picked {
            openWindow(value: ViewerRequest(
                address: address, passcode: passcode, sessionKey: sessionKey,
                sourceId: source.id, name: source.name
            ))
        }
    }
}
