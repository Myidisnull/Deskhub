import AppKit
import SwiftUI
import UserNotifications

@MainActor @Observable
final class AppLifecycle {
    var showBackgroundPrompt = false
    var promptChoiceYes = true
    var isInBackground = false

    private let tray = TrayController()
    private weak var mainWindow: NSWindow?
    private var quitting = false
    private var agent: AgentModel?

    func attach(agent: AgentModel) {
        self.agent = agent
        tray.onRestore = { [weak self] in self?.restoreFromTray() }
        tray.onExit = { [weak self] in self?.exitFromTray() }
        applyBackgroundSetting()
    }

    func bindMainWindow(_ window: NSWindow) {
        mainWindow = window
    }

    func applyBackgroundSetting() {
        guard let agent else { return }
        if agent.runInBackground, !agent.hideTrayIcon {
            tray.show()
            return
        }
        tray.hide()
        if !agent.runInBackground, isInBackground {
            isInBackground = false
            if let window = mainWindow {
                window.makeKeyAndOrderFront(nil)
            }
            NSApp.activate(ignoringOtherApps: true)
        }
    }

    func windowShouldClose(_ window: NSWindow) -> Bool {
        if quitting { return true }
        if showBackgroundPrompt { return false }
        guard let agent else { return true }

        if !agent.runInBackgroundChoiceMade {
            promptChoiceYes = true
            showBackgroundPrompt = true
            return false
        }
        if agent.runInBackground {
            hideToBackground(window)
            return false
        }
        guard confirmQuitIfBusy() else { return false }
        prepareQuit()
        return true
    }

    func confirmBackgroundPrompt() {
        guard let agent else { return }
        showBackgroundPrompt = false
        agent.recordRunInBackground(promptChoiceYes)
        applyBackgroundSetting()
        if promptChoiceYes {
            if let window = mainWindow {
                hideToBackground(window)
            }
        } else {
            guard confirmQuitIfBusy() else { return }
            quitFully()
        }
    }

    func dismissBackgroundPrompt() {
        showBackgroundPrompt = false
        guard confirmQuitIfBusy() else { return }
        quitFully()
    }

    func restoreFromTray() {
        isInBackground = false
        NSApp.setActivationPolicy(.regular)
        if let window = mainWindow {
            window.makeKeyAndOrderFront(nil)
        } else {
            NSApp.windows.first { $0.identifier?.rawValue == "main" }?.makeKeyAndOrderFront(nil)
        }
        NSApp.activate(ignoringOtherApps: true)
        applyBackgroundSetting()
    }

    private func hideToBackground(_ window: NSWindow) {
        mainWindow = window
        window.orderOut(nil)
        isInBackground = true
        NSApp.setActivationPolicy(.accessory)
        applyBackgroundSetting()
        if agent?.hideTrayIcon != true {
            showBackgroundHint()
        }
    }

    private func showBackgroundHint() {
        let center = UNUserNotificationCenter.current()
        center.requestAuthorization(options: [.alert, .provisional]) { granted, _ in
            guard granted else { return }
            let content = UNMutableNotificationContent()
            content.title = DeskhubClient.string(DHStrAppTitle)
            content.body = DeskhubClient.string(DHStrBackgroundRunningHint)
            let request = UNNotificationRequest(
                identifier: "deskhub.background.\(UUID().uuidString)",
                content: content,
                trigger: nil
            )
            center.add(request)
        }
    }

    private func exitFromTray() {
        guard confirmQuitIfBusy() else { return }
        quitFully()
    }

    private func hasActiveSession() -> Bool {
        if agent?.isSharing == true || agent?.isStarting == true { return true }
        return dh_viewers_open()
    }

    private func confirmQuitIfBusy() -> Bool {
        guard hasActiveSession() else { return true }
        let alert = NSAlert()
        alert.messageText = DeskhubClient.string(DHStrAppTitle)
        alert.informativeText = DeskhubClient.string(DHStrQuitWhileBusyMessage)
        alert.alertStyle = .warning
        alert.addButton(withTitle: DeskhubClient.string(DHStrQuitWhileBusyQuit))
        alert.addButton(withTitle: DeskhubClient.string(DHStrQuitWhileBusyCancel))
        return alert.runModal() == .alertFirstButtonReturn
    }

    private func prepareQuit() {
        quitting = true
        tray.hide()
        isInBackground = false
        agent?.stopSharing()
    }

    private func quitFully() {
        prepareQuit()
        NSApp.terminate(nil)
    }
}

struct WindowCloseHook: NSViewRepresentable {
    var lifecycle: AppLifecycle

    func makeNSView(context: Context) -> NSView {
        let view = NSView()
        DispatchQueue.main.async {
            guard let window = view.window else { return }
            lifecycle.bindMainWindow(window)
            window.delegate = context.coordinator
        }
        return view
    }

    func updateNSView(_ nsView: NSView, context: Context) {
        DispatchQueue.main.async {
            guard let window = nsView.window else { return }
            lifecycle.bindMainWindow(window)
            window.delegate = context.coordinator
        }
    }

    func makeCoordinator() -> Coordinator {
        Coordinator(lifecycle: lifecycle)
    }

    final class Coordinator: NSObject, NSWindowDelegate {
        let lifecycle: AppLifecycle

        init(lifecycle: AppLifecycle) {
            self.lifecycle = lifecycle
        }

        func windowShouldClose(_ sender: NSWindow) -> Bool {
            lifecycle.windowShouldClose(sender)
        }
    }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
    func applicationDidFinishLaunching(_: Notification) {
        _ = dh_settings_load()
        _ = dh_log_start_process()
        guard let bundleId = Bundle.main.bundleIdentifier else { return }
        let others = NSRunningApplication.runningApplications(withBundleIdentifier: bundleId)
            .filter { $0 != NSRunningApplication.current }
        guard let other = others.first else { return }
        other.activate(options: [.activateAllWindows, .activateIgnoringOtherApps])
        NSApp.terminate(nil)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_: NSApplication) -> Bool {
        false
    }

    func applicationShouldHandleReopen(_: NSApplication, hasVisibleWindows flag: Bool)
        -> Bool
    {
        if !flag {
            NotificationCenter.default.post(name: .deskhubRestoreRequested, object: nil)
        }
        return true
    }
}

extension Notification.Name {
    static let deskhubRestoreRequested = Notification.Name("deskhubRestoreRequested")
}
