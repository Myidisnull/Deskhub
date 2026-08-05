import SwiftUI

struct ConnectView: View {
    @Bindable var model: SessionModel

    @State private var control = dh_client_control()

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                heading(DeskhubClient.string(DHStrClientHeading))

                TextField(
                    DeskhubClient.string(DHStrClientIpPlaceholder), text: $model.connect.address
                )
                .textFieldStyle(.roundedBorder)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .keyboardType(.numbersAndPunctuation)
                .submitLabel(.go)
                .onSubmit(model.beginConnect)
                .disabled(model.connect.isConnecting)

                VStack(alignment: .leading, spacing: 4) {
                    TextField(
                        DeskhubClient.string(DHStrClientPasscodePrompt),
                        text: $model.connect.passcode
                    )
                    .textFieldStyle(.roundedBorder)
                    .keyboardType(.numberPad)
                    .disabled(model.connect.isConnecting)

                    Text(DeskhubClient.string(DHStrClientPasscodeHint))
                        .font(.caption)
                        .foregroundStyle(DeskhubPalette.muted)
                }

                HStack(spacing: 12) {
                    Button("Connect", action: model.beginConnect)
                        .buttonStyle(.borderedProminent)
                        .disabled(model.connect.address.isEmpty || model.connect.isConnecting)

                    if model.connect.isConnecting {
                        ProgressView()
                        Text(DeskhubClient.string(DHStrQueryingSources))
                            .foregroundStyle(DeskhubPalette.muted)
                    }
                }

                Toggle(DeskhubClient.string(DHStrRequestControlLabel), isOn: $control)
                    .onChange(of: control) { _, on in dh_set_client_control(on) }

                if !model.connect.connectError.isEmpty {
                    Text(model.connect.connectError)
                        .foregroundStyle(.red)
                        .fixedSize(horizontal: false, vertical: true)
                }

                heading(DeskhubClient.string(DHStrLanDevicesHeading))
                DeviceListView(
                    heading: "",
                    note: model.discovery.scanStatus,
                    rows: model.discovery.scanHits.map {
                        DeviceListRow($0, passcode: DeskhubDiscovery.passcode(for: $0.addr))
                    },
                    enabled: !model.connect.isConnecting,
                    onPick: model.pick
                )

                heading(DeskhubClient.string(DHStrRecentDevicesHeading))
                DeviceListView(
                    heading: "",
                    note: DeskhubClient.string(
                        model.discovery.recent.isEmpty
                            ? DHStrRecentDevicesEmpty : DHStrRecentDevicesHint
                    ),
                    rows: model.discovery.recent.map { DeviceListRow($0) },
                    enabled: !model.connect.isConnecting,
                    onPick: model.pick
                )
            }
            .padding()
        }
        .task { model.discovery.start() }
    }

    private func heading(_ text: String) -> some View {
        Text(text)
            .font(.title3.bold())
            .foregroundStyle(DeskhubPalette.heading)
    }
}
