import Foundation
import Observation

@MainActor @Observable
final class FilesHost {
    static let shared = FilesHost()

    private(set) var receiving = false

    private init() {}

    func run() async {
        while !Task.isCancelled {
            sync()
            await ReceivedFiles.sweep(sharing: BroadcastStatus.broadcastProcessAlive)
            try? await Task.sleep(for: .seconds(1))
        }
        stop()
    }

    func stop() {
        guard receiving else { return }
        dha_stop()
        receiving = false
    }

    private func sync() {
        let wanted = dh_take_files() && !BroadcastStatus.broadcastProcessAlive
        if wanted, !receiving { start() }
        if !wanted, receiving { stop() }
        if receiving, !dha_running() { receiving = false }
    }

    private func start() {
        guard let dir = ReceivedFiles.incomingDir else { return }
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        dh_set_transfer_dir(dir.path)
        let stored = dh_settings_load()
        let passcode = DeskhubClient.cString(stored.passcode)
        receiving = dha_start(
            nil, 0, 0, 0, 0, UInt16(stored.port), false, passcode, false, true
        )
    }
}
