import Observation
import SwiftUI

@MainActor @Observable
final class ViewerRegistry {
    var count = 0
}

@main
struct DeskhubApp: App {
    @State private var viewers = ViewerRegistry()

    var body: some Scene {
        Window("Deskhub", id: "main") {
            ContentView()
                .environment(viewers)
        }
        .windowResizability(.contentSize)

        WindowGroup(id: "viewer", for: ViewerRequest.self) { $request in
            if let request {
                ViewerWindow(request: request)
                    .environment(viewers)
            }
        }
        .windowResizability(.contentMinSize)
        .defaultSize(width: 1024, height: 634)
    }
}
