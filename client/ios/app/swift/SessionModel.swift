import Foundation
import Observation

@MainActor @Observable
final class SessionModel {
    var connect = ConnectModel()
    var discovery = DiscoveryModel()
    var settings = SettingsModel()
    var sharing = SharingModel()

    var screen: ClientRoute = .connect
    var sources: [Source] = []
    private(set) var stream: StreamModel?
    private(set) var terminal: TerminalModel?
    var fileSend: FileSendModel?

    func beginConnect(to address: String, passcode: String) {
        connect.address = address
        connect.passcode = passcode
        beginConnect()
    }

    func beginConnect() {
        guard !connect.isConnecting else { return }
        connect.saveDeviceName()
        Task {
            let found = await connect.listSources()
            guard !connect.acceptedAddress.isEmpty, connect.connectError.isEmpty else { return }
            await discovery.remember(
                address: connect.acceptedAddress, passcode: connect.acceptedPasscode
            )
            sources = found
            let decision = DeskhubClient.connectDecision(found)
            if decision.showPicker {
                screen = .sourcePicker(found)
            } else {
                startStream(sourceId: decision.sourceId)
            }
        }
    }

    func startStream(sourceId: UInt8) {
        connect.connectError = ""
        let address = connect.acceptedAddress
        let model = StreamModel(
            address: address, passcode: connect.acceptedPasscode, sourceId: sourceId,
            sourceName: sourceName(of: sourceId)
        )
        stream = model
        screen = .stream

        Task {
            await model.start()
            guard model.failedToStart, stream === model else { return }
            stream = nil
            screen = .connect
            connect.connectError = DeskhubClient.couldNotConnect(address) + " "
                + DeskhubClient.string(DHStrInvalidAddressHint)
        }
    }

    func switchSource(to sourceId: UInt8) {
        guard let stream else { return }
        let name = sourceName(of: sourceId)
        Task { await stream.switchSource(to: sourceId, name: name) }
    }

    func disconnect() {
        stream?.disconnect()
        stream = nil
        screen = .connect
    }

    func openFileSend() {
        guard let accepted = connect.acceptAddress() else { return }
        connect.saveDeviceName()
        let passcode = connect.acceptedPasscode
        let name = connect.deviceName
        Task {
            connect.isConnecting = true
            let query = await Task.detached {
                DeskhubClient.listSources(address: accepted, passcode: passcode)
            }.value
            connect.isConnecting = false
            guard let query, query.caps.files else {
                connect.connectError = DeskhubClient.string(DHStrTransferHostNotTaking)
                return
            }
            await discovery.remember(address: accepted, passcode: passcode)
            let sender = FileSendModel()
            sender.address = accepted
            sender.passcode = passcode
            sender.deviceName = name
            fileSend = sender
        }
    }

    func closeFileSend() {
        fileSend?.forgetTransfer()
        clearSendStaging()
        fileSend = nil
    }

    func openShell() {
        guard let accepted = connect.acceptAddress() else { return }
        connect.saveDeviceName()
        let passcode = connect.acceptedPasscode
        Task {
            connect.isConnecting = true
            let shared = await Task.detached {
                DeskhubClient.hostHasTerminal(address: accepted, passcode: passcode)
            }.value
            connect.isConnecting = false
            guard shared else {
                connect.connectError = DeskhubClient.string(DHStrHostHasNoTerminal)
                return
            }
            await discovery.remember(address: accepted, passcode: passcode)
            let model = TerminalModel()
            guard model.open(address: accepted, passcode: passcode) else {
                connect.connectError = DeskhubClient.couldNotConnect(accepted)
                return
            }
            terminal = model
            screen = .terminal
        }
    }

    func closeShell() {
        terminal?.stop()
        terminal = nil
        screen = .connect
    }

    private func sourceName(of sourceId: UInt8) -> String {
        sources.first { $0.id == sourceId }?.name ?? ""
    }
}
