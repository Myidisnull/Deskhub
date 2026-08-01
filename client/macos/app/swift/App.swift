import SwiftUI

@main
struct DeskhubApp: App {
    var body: some Scene {
        Window("Deskhub", id: "main") {
            ContentView()
        }
        .windowResizability(.contentSize)

        WindowGroup(id: "viewer", for: ViewerRequest.self) { $request in
            if let request {
                ViewerWindow(request: request)
            }
        }
        .windowResizability(.contentMinSize)
        .defaultSize(width: 1024, height: 634)
    }
}
