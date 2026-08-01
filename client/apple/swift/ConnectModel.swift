import Foundation
import Observation

@MainActor @Observable
final class ConnectModel {
    private static let lastAddressKey = "lastAddress"

    var address: String = UserDefaults.standard.string(forKey: ConnectModel.lastAddressKey) ?? ""
    var isConnecting = false
    var connectError = ""

    func acceptAddress() -> String? {
        guard !address.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return nil }
        guard let accepted = DeskhubClient.normalizedAddress(address) else {
            connectError = "Invalid address: \"\(address)\". "
                + DeskhubClient.string(DHStrInvalidAddressHint)
            return nil
        }
        address = accepted
        connectError = ""
        UserDefaults.standard.set(accepted, forKey: ConnectModel.lastAddressKey)
        return accepted
    }

    func listSources() async -> [Source] {
        guard let accepted = acceptAddress() else { return [] }
        isConnecting = true
        defer { isConnecting = false }
        return await Task.detached { DeskhubClient.listSources(address: accepted) }.value
    }
}
