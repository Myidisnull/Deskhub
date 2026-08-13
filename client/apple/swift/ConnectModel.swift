import Foundation
import Observation

#if os(macOS)
    import AppKit
#else
    import UIKit
#endif

@MainActor @Observable
final class ConnectModel {
    private static let lastAddressKey = "lastAddress"
    private static let retiredPasscodeKey = "lastPasscode"

    private static var lastAddress: String {
        UserDefaults.standard.removeObject(forKey: retiredPasscodeKey)
        return UserDefaults.standard.string(forKey: lastAddressKey) ?? ""
    }

    private static var defaultDeviceName: String {
        #if os(macOS)
            Host.current().localizedName ?? ProcessInfo.processInfo.hostName
        #else
            UIDevice.current.name
        #endif
    }

    private static var initialDeviceName: String {
        let stored = DeskhubClient.buffered(96) { dh_device_name($0, $1) }
        return stored.isEmpty ? ConnectModel.defaultDeviceName : stored
    }

    var address: String = DeskhubClient.addressHost(ConnectModel.lastAddress)
    var port: String = DeskhubClient.addressPortText(ConnectModel.lastAddress)
    var passcode: String = DeskhubDiscovery.passcode(for: ConnectModel.lastAddress)
    var deviceName: String = ConnectModel.initialDeviceName
    var isConnecting = false
    var connectError = ""
    private(set) var acceptedAddress = ""

    var acceptedPasscode: String {
        DeskhubClient.isValidPasscode(passcode) ? passcode : ""
    }

    func saveDeviceName() {
        deviceName = deviceName.trimmingCharacters(in: .whitespacesAndNewlines)
        if deviceName.isEmpty { deviceName = ConnectModel.defaultDeviceName }
        dh_set_device_name(deviceName)
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
