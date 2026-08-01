import Foundation
import Observation

@MainActor @Observable
final class SessionModel {
    var address: String = UserDefaults.standard.string(forKey: "lastAddress") ?? ""
    var isConnecting = false
    var connectError = ""

    func listSources() async -> [Source] {
        let trimmed = address.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return [] }
        guard let addr = DeskhubClient.normalizedAddress(trimmed) else {
            connectError = "Invalid address: \"\(trimmed)\". Enter just the IP address."
            return []
        }
        address = addr
        isConnecting = true
        connectError = ""
        UserDefaults.standard.set(addr, forKey: "lastAddress")

        let found = await Task.detached { DeskhubClient.listSources(address: addr) }.value
        isConnecting = false
        return found
    }
}
