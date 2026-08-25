import SwiftUI

struct ConnectedView: View {
    @Bindable var model: SessionModel

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text(DeskhubClient.string(DHStrConnectedPickSession))
                .font(.headline)

            Text(model.connect.acceptedAddress)
                .foregroundStyle(.secondary)
                .textSelection(.enabled)

            if !model.sources.isEmpty {
                Button(DeskhubClient.string(DHStrOpenDesktopLabel)) {
                    model.openDesktop()
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)

                if model.hostCaps.files {
                    Button(DeskhubClient.string(DHStrOpenFilesLabel)) {
                        model.openFiles()
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.large)
                }

                if model.hostCaps.terminal {
                    Button(DeskhubClient.string(DHStrOpenShellLabel)) {
                        model.openShell()
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.large)
                }
            } else if model.hostCaps.files {
                Text(DeskhubClient.string(DHStrAcceptFilesLabel))
                    .foregroundStyle(.secondary)
            } else if model.hostCaps.terminal {
                Button(DeskhubClient.string(DHStrOpenShellLabel)) {
                    model.openShell()
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
            }

            Button(DeskhubClient.string(DHStrDisconnectButton)) {
                model.disconnect()
            }
            .buttonStyle(.bordered)

            Spacer()
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding()
        .navigationTitle(DeskhubClient.string(DHStrClientHeading))
    }
}
