import SwiftUI

struct ConnectView: View {
    @Bindable var model: SessionModel

    @State private var prompting: DeviceListRow?
    @State private var promptPasscode = ""
    @State private var promptPort = ""

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                deskhubHeading(DeskhubClient.string(DHStrClientHeading))

                HStack(spacing: 12) {
                    TextField(
                        DeskhubClient.string(DHStrClientIpPlaceholder),
                        text: $model.connect.address
                    )
                    .textFieldStyle(.roundedBorder)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .keyboardType(.numbersAndPunctuation)
                    .submitLabel(.go)
                    .onSubmit(model.beginConnect)
                    .disabled(model.connect.isConnecting)

                    TextField(
                        DeskhubClient.string(DHStrUdpPortLabel), text: $model.connect.port
                    )
                    .textFieldStyle(.roundedBorder)
                    .keyboardType(.numberPad)
                    .frame(width: 90)
                    .disabled(model.connect.isConnecting)
                }

                VStack(alignment: .leading, spacing: 4) {
                    PasscodeField(
                        passcode: $model.connect.passcode,
                        prompt: DeskhubClient.string(DHStrClientPasscodePrompt),
                        enabled: !model.connect.isConnecting
                    )

                    Text(DeskhubClient.string(DHStrClientPasscodeHint))
                        .font(.caption)
                        .foregroundStyle(DeskhubPalette.muted)
                }

                TextField("Your name", text: $model.connect.deviceName)
                    .textFieldStyle(.roundedBorder)
                    .autocorrectionDisabled()
                    .submitLabel(.go)
                    .onSubmit(model.beginConnect)
                    .disabled(model.connect.isConnecting)

                Button(action: model.beginConnect) {
                    Text("Connect").deskhubPrimaryLabel()
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
                .tint(DeskhubPalette.accent)
                .disabled(model.connect.address.isEmpty || model.connect.isConnecting)

                Button(action: model.openShell) {
                    Text(DeskhubClient.string(DHStrOpenShellLabel)).deskhubPrimaryLabel()
                }
                .buttonStyle(.bordered)
                .controlSize(.large)
                .disabled(model.connect.address.isEmpty || model.connect.isConnecting)

                if model.connect.isConnecting {
                    HStack(spacing: 12) {
                        ProgressView()
                        Text(DeskhubClient.string(DHStrQueryingSources))
                            .foregroundStyle(DeskhubPalette.muted)
                    }
                }

                Toggle(
                    DeskhubClient.string(DHStrRequestControlLabel),
                    isOn: $model.settings.clientControl
                )
                .onChange(of: model.settings.clientControl) { _, _ in model.settings.save() }

                if !model.connect.connectError.isEmpty {
                    Text(model.connect.connectError)
                        .foregroundStyle(.red)
                        .fixedSize(horizontal: false, vertical: true)
                }

                deskhubHeadingRow(DeskhubClient.string(DHStrDevicesHeading)) {
                    model.discovery.refreshStatus()
                    model.discovery.rescanNow()
                }
                DeviceListView(
                    heading: "",
                    note: model.discovery.scanStatus,
                    rows: model.discovery.devices,
                    enabled: !model.connect.isConnecting,
                    onPick: pick
                )
            }
            .padding()
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
        .task { model.discovery.start() }
    }

    private func pick(_ row: DeviceListRow) {
        promptPasscode = DeskhubClient.isValidPasscode(row.passcode)
            ? row.passcode : model.connect.passcode
        promptPort = DeskhubClient.addressPortText(row.addr)
        prompting = row
    }

    private func confirmPrompt(_ row: DeviceListRow) {
        prompting = nil
        let addr = DeskhubClient.composeAddress(
            DeskhubClient.addressHost(row.addr), portText: promptPort
        )
        model.beginConnect(to: addr, passcode: promptPasscode)
    }
}
