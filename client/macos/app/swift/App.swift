import SwiftUI

@main
struct DeskhubApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @State private var agent = AgentModel()

    var body: some Scene {
        Window("Deskhub", id: "main") {
            ContentView(agent: agent)
                .onAppear {
                    NSApp.setActivationPolicy(.regular)
                }
                .onDisappear {
                    if agent.startHidden { NSApp.setActivationPolicy(.accessory) }
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

        MenuBarExtra("Deskhub", systemImage: "rectangle.on.rectangle",
                     isInserted: Bindable(agent).startHidden)
        {
            TrayMenu(agent: agent)
        }
    }
}

private struct TrayMenu: View {
    var agent: AgentModel
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        Button(DeskhubClient.string(DHStrTrayShowWindow)) { showMainWindow() }
        Button(DeskhubClient.string(agent.isSharing ? DHStrStopSharing : DHStrStartSharing)) {
            toggleSharing()
        }
        Divider()
        Button(DeskhubClient.string(DHStrTrayQuit)) { NSApp.terminate(nil) }
    }

    private func showMainWindow() {
        NSApp.setActivationPolicy(.regular)
        openWindow(id: "main")
        NSApp.activate(ignoringOtherApps: true)
    }

    private func toggleSharing() {
        if agent.isSharing {
            agent.stopSharing()
            return
        }
        Task {
            if await !agent.startSharing() { showMainWindow() }
        }
    }
}
