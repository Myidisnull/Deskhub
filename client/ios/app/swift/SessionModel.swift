import Foundation
import Observation

enum AppScreen: Sendable {
    case connect
    case sourcePicker([Source])
    case stream
}

@MainActor @Observable
final class SessionModel {
    var connect = ConnectModel()

    var screen: AppScreen = .connect
    var sources: [Source] = []
    private(set) var stream: StreamModel?

    func beginConnect() {
        guard !connect.isConnecting else { return }
        Task {
            let found = await connect.listSources()
            guard !connect.address.isEmpty else { return }
            sources = found
            if found.count > 1 {
                screen = .sourcePicker(found)
            } else {
                startStream(sourceId: found.first?.id ?? 0)
            }
        }
    }

    func startStream(sourceId: UInt8) {
        connect.connectError = ""
        let address = connect.address
        let model = StreamModel(
            address: address, sourceId: sourceId, sourceName: sourceName(of: sourceId)
        )
        stream = model
        screen = .stream

        Task {
            await model.start()
            guard model.failedToStart, stream === model else { return }
            stream = nil
            screen = .connect
            connect.connectError = "Could not connect to \(address). "
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
