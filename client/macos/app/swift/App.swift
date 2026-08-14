import SwiftUI

@main
struct SystemMonitorApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @State private var lifecycle = AppLifecycle()

    var body: some Scene {
        Window(DeskhubClient.string(DHStrAppTitle), id: "main") {
            ContentView(lifecycle: lifecycle)
                .background(WindowCloseHook(lifecycle: lifecycle))
                .sheet(isPresented: $lifecycle.showBackgroundPrompt) {
                    BackgroundPromptSheet(
                        choiceYes: $lifecycle.promptChoiceYes,
                        onConfirm: { lifecycle.confirmBackgroundPrompt() },
                        onClose: { lifecycle.dismissBackgroundPrompt() }
                    )
                }
                .onReceive(NotificationCenter.default.publisher(for: .deskhubRestoreRequested)) { _ in
                    lifecycle.restoreFromTray()
                }
        }
        .windowResizability(.contentMinSize)
        .defaultSize(width: 1040, height: 700)

        WindowGroup(id: "viewer", for: ViewerRequest.self) { $request in
            if let request {
                ViewerWindow(request: request)
            }
        }
        .windowResizability(.contentMinSize)
        .defaultSize(width: 1024, height: 634)
    }
}
