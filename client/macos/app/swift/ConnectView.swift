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

func deskhubHeading(_ text: String) -> some View {
    Text(text)
        .font(.system(size: 19, weight: .bold))
        .foregroundStyle(DeskhubPalette.heading)
}

func deskhubSection(_ text: String) -> some View {
    Text(text)
        .font(.system(size: 15, weight: .bold))
        .foregroundStyle(DeskhubPalette.heading)
        .padding(.top, 8)
}

func deskhubHint(_ text: String) -> some View {
    Text(text).foregroundStyle(DeskhubPalette.muted)
}

struct SettingsPage: View {
    @Bindable var agent: AgentModel

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            deskhubHeading(DeskhubClient.string(DHStrSettingsHeading))
            deskhubHint(DeskhubClient.string(DHStrSettingsHint))

            deskhubSection("Video")
            Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 10) {
                GridRow {
                    Text("FPS")
                    TextField("", value: $agent.fps, format: .number)
                        .textFieldStyle(.roundedBorder).frame(width: 90)
                }
                GridRow {
                    Text("Bitrate (Mbps)")
                    TextField("", value: $agent.bitrateMbps, format: .number)
                        .textFieldStyle(.roundedBorder).frame(width: 90)
                }
                GridRow {
                    Text("Quality")
                    Picker("", selection: $agent.maxDim) {
                        ForEach(DeskhubAgent.qualityPresets) { preset in
                            Text(preset.label).tag(preset.maxDim)
                        }
                    }
                    .labelsHidden()
                    .frame(width: 120)
                }
            }

            deskhubSection("Connection")
            Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 10) {
                GridRow {
                    Text("UDP port")
                    TextField("", value: $agent.port, format: .number)
                        .textFieldStyle(.roundedBorder).frame(width: 90)
                }
            }

            deskhubSection("Security")
            Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 10) {
                GridRow {
                    Text(DeskhubClient.string(DHStrPasscodeLabel))
                    TextField("", text: $agent.passcode)
                        .textFieldStyle(.roundedBorder).frame(width: 64)
                }
            }

            PermissionsSection(agent: agent)
        }
        .onChange(of: agent.fps) { _, _ in agent.save() }
        .onChange(of: agent.bitrateMbps) { _, _ in agent.save() }
        .onChange(of: agent.maxDim) { _, _ in agent.save() }
        .onChange(of: agent.port) { _, _ in agent.save() }
        .onChange(of: agent.passcode) { _, _ in agent.save() }
        .onChange(of: agent.allowInput) { _, _ in agent.save() }
    }
}

struct HostAddressList: View {
    let addresses: [LocalAddress]

    var body: some View {
        if addresses.isEmpty {
            Text(DeskhubClient.string(DHStrNoNetworkAddress)).foregroundStyle(.secondary)
        } else {
            ForEach(addresses) { addr in
                HStack(spacing: 14) {
                    Text(addr.name).frame(width: 150, alignment: .leading).lineLimit(1)
                    Text(addr.ip).fontWeight(.bold).textSelection(.enabled)
                    Spacer(minLength: 0)
                    Button("Copy") {
                        NSPasteboard.general.clearContents()
                        NSPasteboard.general.setString(addr.ip, forType: .string)
                    }
                    .frame(width: 84)
                }
            }
        }
    }
}

struct MainMenuView: View {
    @Binding var route: ClientRoute
    @Bindable var connect: ConnectModel
    @Bindable var agent: AgentModel

    @State private var discovery = DiscoveryModel()
    @State private var page: DeskhubPage = .client
    @State private var shareAlert = ""
    @State private var accessibilityWarning = false
    @Environment(\.openWindow) private var openWindow
    @Environment(\.dismissWindow) private var dismissWindow

    var body: some View {
        HStack(spacing: 0) {
            sidebar
            Divider()
            ScrollView {
                page(for: page).padding(16)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(Color(nsColor: .textBackgroundColor))
        }
        .task {
            agent.refreshPermissions()
            agent.loadAddresses()
            discovery.start()
        }
        .alert("Deskhub", isPresented: showingShareAlert) {
            if !agent.hasScreenRecording {
                Button("Grant Screen Recording") { agent.requestScreenRecording() }
            }
            Button("OK", role: .cancel) {}
        } message: {
            Text(shareAlert)
        }
        .alert("Deskhub", isPresented: $accessibilityWarning) {
            Button("Share anyway") { Task { await doShare() } }
            Button("Grant Accessibility", role: .cancel) {
                agent.requestAccessibility()
            }
        } message: {
            Text("Mouse and keyboard are always shared, but macOS silently drops "
                + "them until Deskhub has Accessibility permission. The other "
                + "machine will see this Mac but not control it.")
        }
    }

    private var sidebar: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text("Deskhub")
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
        case .host: hostPage
        case .client: clientPage
        case .settings: SettingsPage(agent: agent)
        }
    }

    private var hostPage: some View {
        VStack(alignment: .leading, spacing: 10) {
            deskhubHeading(DeskhubClient.string(DHStrHostHeading))
            deskhubHint(DeskhubClient.string(DHStrHostIpIntro))

            HostAddressList(addresses: agent.addresses)

            Text(agent.statusLine)
                .fontWeight(.bold)
                .foregroundStyle(agent.isSharing ? DeskhubPalette.online : DeskhubPalette.muted)
                .fixedSize(horizontal: false, vertical: true)

            HostSourceTable(rows: agent.rows)
                .frame(minHeight: 170)

            Button {
                Task { await share() }
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
                    Text(DeskhubClient.string(DHStrClientPasscodePrompt))
                    TextField("", text: $connect.passcode)
                        .textFieldStyle(.roundedBorder)
                        .frame(width: 64)
                        .help(DeskhubClient.string(DHStrClientPasscodeHint))
                        .onSubmit(beginConnect)
                        .disabled(connect.isConnecting)
                }
            }

            Button("Connect", action: beginConnect)
                .buttonStyle(.borderedProminent)
                .frame(width: 140)
                .frame(maxWidth: .infinity, alignment: .center)
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

            deskhubHeading(DeskhubClient.string(DHStrLanDevicesHeading))
            DeviceListView(
                heading: "",
                note: discovery.scanStatus,
                rows: discovery.scanHits.map {
                    DeviceListRow($0, passcode: DeskhubDiscovery.passcode(for: $0.addr))
                },
                enabled: !connect.isConnecting,
                onPick: pick
            )

            deskhubHeading(DeskhubClient.string(DHStrRecentDevicesHeading))
            DeviceListView(
                heading: "",
                note: DeskhubClient.string(
                    discovery.recent.isEmpty ? DHStrRecentDevicesEmpty : DHStrRecentDevicesHint
                ),
                rows: discovery.recent.map { DeviceListRow($0) },
                enabled: !connect.isConnecting,
                onPick: pick
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
        if await !(agent.startSharing()) {
            shareAlert = agent.startError
        }
    }

    private func pick(_ row: DeviceListRow) {
        connect.address = row.addr
        if DeskhubClient.isValidPasscode(row.passcode) { connect.passcode = row.passcode }
        beginConnect()
    }

    private func beginConnect() {
        guard !connect.address.isEmpty, !connect.isConnecting else { return }
        Task {
            let sources = await connect.listSources()
            guard connect.connectError.isEmpty else { return }
            await discovery.remember(
                address: connect.address, passcode: connect.acceptedPasscode
            )
            if DeskhubClient.connectDecision(sources).showPicker {
                route = .sourcePicker(sources)
            } else {
                openViewers(sources, address: connect.address,
                            passcode: connect.acceptedPasscode,
                            openWindow: openWindow, dismissWindow: dismissWindow)
            }
        }
    }
}

struct HostSourceTable: View {
    let rows: [AgentSourceStatus]

    var body: some View {
        Table(rows) {
            TableColumn("Source") { Text($0.name) }.width(140)
            TableColumn("Size") { Text($0.size) }.width(80)
            TableColumn("Viewers") { Text("\($0.viewerCount)") }.width(58)
            TableColumn("Client") { Text($0.viewerAddr) }.width(120)
            TableColumn("Capture") { Text($0.captureFps) }.width(58)
            TableColumn("Send") { Text($0.sendFps) }.width(50)
            TableColumn("Mbps") { Text($0.sendMbps) }.width(55)
            TableColumn("RTT") { Text($0.rtt) }.width(55)
        }
    }
}

@MainActor
func openViewers(_ picked: [Source], address: String, passcode: String,
                 openWindow: OpenWindowAction, dismissWindow: DismissWindowAction)
{
    if picked.isEmpty {
        openWindow(value: ViewerRequest(
            address: address, passcode: passcode, sourceId: 0, name: ""
        ))
    } else {
        for source in picked {
            openWindow(value: ViewerRequest(
                address: address, passcode: passcode, sourceId: source.id, name: source.name
            ))
        }
    }
    dismissWindow(id: "main")
}
