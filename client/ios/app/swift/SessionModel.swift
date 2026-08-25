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
    var hostCaps = HostCaps()
    var openFilePickerOnStream = false
    private(set) var stream: StreamModel?
    private(set) var terminal: TerminalModel?

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
                address: connect.acceptedAddress, passcode: connect.acceptedPasscode,
                sessionKey: connect.acceptedSessionKey
            )
            sources = found.sources
            hostCaps = found.caps
            openFilePickerOnStream = false
            screen = .connected
        }
    }

    func openDesktop() {
        openFilePickerOnStream = false
        let decision = DeskhubClient.connectDecision(sources)
        if decision.showPicker {
            screen = .sourcePicker(sources)
        } else if !sources.isEmpty {
            startStream(sourceId: decision.sourceId)
        }
    }

    func openFiles() {
        guard hostCaps.files, let first = sources.first else { return }
        openFilePickerOnStream = true
        if sources.count > 1 {
            screen = .sourcePicker(sources)
        } else {
            startStream(sourceId: first.id)
        }
    }

    func openShell() {
        guard hostCaps.terminal else { return }
        connect.connectError = ""
        let model = TerminalModel()
        guard model.open(address: connect.acceptedAddress, passcode: connect.acceptedPasscode) else {
            connect.connectError = DeskhubClient.couldNotConnect(connect.acceptedAddress) + " "
                + DeskhubClient.string(DHStrInvalidAddressHint)
            return
        }
        terminal = model
        screen = .terminal
    }

    func startStream(sourceId: UInt8) {
        connect.connectError = ""
        let address = connect.acceptedAddress
        let model = StreamModel(
            address: address, passcode: connect.acceptedPasscode, sourceId: sourceId,
            sourceName: sourceName(of: sourceId), sessionKey: connect.acceptedSessionKey
        )
        stream = model
        screen = .stream

        Task {
            await model.start()
            guard model.failedToStart, stream === model else { return }
            stream = nil
            screen = .connected
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
        terminal?.stop()
        terminal = nil
        openFilePickerOnStream = false
        sources = []
        hostCaps = HostCaps()
        screen = .connect
    }

    func leaveSession() {
        stream?.disconnect()
        stream = nil
        openFilePickerOnStream = false
        screen = .connected
    }

    func leaveTerminal() {
        terminal?.stop()
        terminal = nil
        screen = .connected
    }

    private func sourceName(of sourceId: UInt8) -> String {
        sources.first { $0.id == sourceId }?.name ?? ""
    }
}
