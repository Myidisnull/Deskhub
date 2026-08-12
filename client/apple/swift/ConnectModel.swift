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

    var address: String = DeskhubClient.addressHost(ConnectModel.lastAddress)
    var port: String = DeskhubClient.addressPortText(ConnectModel.lastAddress)
    var passcode: String = DeskhubDiscovery.passcode(for: ConnectModel.lastAddress)
    var isConnecting = false
    var connectError = ""
    private(set) var acceptedAddress = ""

    var acceptedPasscode: String {
        DeskhubClient.isValidPasscode(passcode) ? passcode : ""
    }

    func acceptAddress() -> String? {
        acceptedAddress = ""
        guard !address.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return nil }
        let composed = DeskhubClient.composeAddress(address, portText: port)
        guard let accepted = DeskhubClient.normalizedAddress(composed) else {
            connectError = "Invalid address: \"\(composed)\". "
                + DeskhubClient.string(DHStrInvalidAddressHint)
            return nil
        }
        guard DeskhubClient.isValidPasscode(passcode) else {
            connectError = DeskhubClient.string(DHStrPasscodeInvalid)
            return nil
        }
        address = DeskhubClient.addressHost(accepted)
        port = DeskhubClient.addressPortText(accepted)
        acceptedAddress = accepted
        connectError = ""
        UserDefaults.standard.set(accepted, forKey: ConnectModel.lastAddressKey)
        return accepted
    }

    func listSources() async -> [Source] {
        guard let accepted = acceptAddress() else { return [] }
        let code = acceptedPasscode
        isConnecting = true
        defer { isConnecting = false }
        let found = await Task.detached {
            DeskhubClient.listSources(address: accepted, passcode: code)
        }.value
        guard let found else {
            connectError = DeskhubClient.sourceQueryFailed(accepted)
            return []
        }
        guard !found.isEmpty else {
            connectError = DeskhubClient.sourceQueryEmpty(accepted)
            return []
        }
        return found
    }
}
