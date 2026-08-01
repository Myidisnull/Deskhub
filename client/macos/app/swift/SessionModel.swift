import Foundation
import Observation

@MainActor @Observable
final class SessionModel {
    var connect = ConnectModel()

    func listSources() async -> [Source] {
        await connect.listSources()
    }
}
