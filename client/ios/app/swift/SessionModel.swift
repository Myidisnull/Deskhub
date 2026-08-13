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

    private func sourceName(of sourceId: UInt8) -> String {
        sources.first { $0.id == sourceId }?.name ?? ""
    }
}
