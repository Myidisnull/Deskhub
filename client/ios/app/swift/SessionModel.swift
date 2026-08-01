import Foundation
import Observation

enum AppScreen: Sendable {
    case connect
    case sourcePicker([Source])
    case stream
}

@MainActor @Observable
final class SessionModel {
    var screen: AppScreen = .connect
    var address: String = UserDefaults.standard.string(forKey: "lastAddress") ?? ""
    var isConnecting = false
    var connectError = ""

    var sources: [Source] = []
    private(set) var stream: StreamModel?

    func connect() {
        guard !isConnecting else { return }
        let trimmed = address.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        guard let addr = DeskhubClient.normalizedAddress(trimmed) else {
            connectError = "Invalid address: \"\(trimmed)\". Enter just the IP address."
            return
        }
        address = addr
        isConnecting = true
        connectError = ""
        UserDefaults.standard.set(addr, forKey: "lastAddress")

        Task {
            let found = await Task.detached { DeskhubClient.listSources(address: addr) }.value
            isConnecting = false
            sources = found
            if found.count > 1 {
                screen = .sourcePicker(found)
            } else {
                startStream(sourceId: found.first?.id ?? 0)
            }
        }
    }

    func startStream(sourceId: UInt8) {
        connectError = ""
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
            connectError = "Could not connect to \(address). Enter just the IP address."
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
