import SwiftUI

@main
struct DeskhubApp: App {
    init() {
        if let container = BroadcastStatus.containerURL?.path {
            dh_set_data_dir(container)
        }
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
        }
    }
}
