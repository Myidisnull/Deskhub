import Foundation
import Observation

@MainActor @Observable
final class ConnectModel {
    private static let lastAddressKey = "lastAddress"
    private static let retiredPasscodeKey = "lastPasscode"

    private static var lastAddress: String {
        UserDefaults.standard.removeObject(forKey: retiredPasscodeKey)
        return UserDefaults.standard.string(forKey: lastAddressKey) ?? ""
    }

    var address: String = ConnectModel.lastAddress
    var passcode: String = DeskhubDiscovery.passcode(for: ConnectModel.lastAddress)
    var isConnecting = false
    var connectError = ""

    var acceptedPasscode: String {
        DeskhubClient.isValidPasscode(passcode) ? passcode : ""
    }

    func acceptAddress() -> String? {
        guard !address.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return nil }
        guard let accepted = DeskhubClient.normalizedAddress(address) else {
            connectError = "Invalid address: \"\(address)\". "
                + DeskhubClient.string(DHStrInvalidAddressHint)
            return nil
        }
        guard DeskhubClient.isValidPasscode(passcode) else {
            connectError = DeskhubClient.string(DHStrPasscodeInvalid)
            return nil
        }
        address = accepted
        connectError = ""
        UserDefaults.standard.set(accepted, forKey: ConnectModel.lastAddressKey)
        return accepted
    }

    func listSources() async -> [Source] {
        guard let accepted = acceptAddress() else { return [] }
        let code = acceptedPasscode
        isConnecting = true
        defer { isConnecting = false }
        return await Task.detached {
            DeskhubClient.listSources(address: accepted, passcode: code)
        }.value
    }
}
