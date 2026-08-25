import SwiftUI

struct ConnectedView: View {
    @Binding var route: ClientRoute
    @Bindable var connect: ConnectModel
    let sources: [Source]
    let caps: HostCaps
    var openFilesIntent: Binding<Bool>
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text(DeskhubClient.string(DHStrConnectedPickSession))
                .font(.headline)

            Text(connect.acceptedAddress)
                .foregroundStyle(.secondary)
                .textSelection(.enabled)

            if !sources.isEmpty {
                Button(DeskhubClient.string(DHStrOpenDesktopLabel)) {
                    openDesktop()
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)

                if caps.files {
                    Button(DeskhubClient.string(DHStrOpenFilesLabel)) {
                        openFiles()
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.large)
                }

                if caps.terminal {
                    Button(DeskhubClient.string(DHStrOpenShellLabel)) {
                        openShell()
                    }
                    .buttonStyle(.bordered)
                    .controlSize(.large)
                }
            } else if caps.files {
                Text(DeskhubClient.string(DHStrAcceptFilesLabel))
                    .foregroundStyle(.secondary)
            } else if caps.terminal {
                Button(DeskhubClient.string(DHStrOpenShellLabel)) {
                    openShell()
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
            }

            Button(DeskhubClient.string(DHStrDisconnectButton)) {
                route = .connect
            }
            .buttonStyle(.bordered)

            Spacer()
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding()
    }

    private func openDesktop() {
        openFilesIntent.wrappedValue = false
        let decision = DeskhubClient.connectDecision(sources)
        if decision.showPicker {
            route = .sourcePicker(sources)
        } else {
            openViewers(sources, address: connect.acceptedAddress,
                        passcode: connect.acceptedPasscode,
                        sessionKey: connect.acceptedSessionKey,
                        openFiles: false, openWindow: openWindow)
            route = .connect
        }
    }

    private func openFiles() {
        guard caps.files, let first = sources.first else { return }
        openFilesIntent.wrappedValue = true
        if sources.count > 1 {
            route = .sourcePicker(sources)
        } else {
            openViewers([first], address: connect.acceptedAddress,
                        passcode: connect.acceptedPasscode,
                        sessionKey: connect.acceptedSessionKey,
                        openFiles: true, openWindow: openWindow)
            route = .connect
        }
    }

    private func openShell() {
        guard caps.terminal else { return }
        openWindow(value: TerminalRequest(
            address: connect.acceptedAddress, passcode: connect.acceptedPasscode
        ))
        route = .connect
    }
}
