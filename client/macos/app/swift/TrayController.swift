import AppKit

@MainActor
final class TrayController: NSObject {
    private var statusItem: NSStatusItem?
    var onRestore: (() -> Void)?
    var onExit: (() -> Void)?

    func show() {
        guard statusItem == nil else { return }
        let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        if let button = item.button {
            button.image = NSApp.applicationIconImage
            button.image?.size = NSSize(width: 18, height: 18)
            button.imageScaling = .scaleProportionallyDown
            button.target = self
            button.action = #selector(handleClick(_:))
            button.sendAction(on: [.leftMouseUp, .rightMouseUp])
        }
        statusItem = item
    }

    func hide() {
        if let item = statusItem {
            NSStatusBar.system.removeStatusItem(item)
        }
        statusItem = nil
    }

    @objc private func handleClick(_ sender: NSStatusBarButton) {
        guard let event = NSApp.currentEvent else {
            onRestore?()
            return
        }
        if event.type == .rightMouseUp {
            showMenu(sender)
        } else {
            onRestore?()
        }
    }

    private func showMenu(_ sender: NSStatusBarButton) {
        let menu = NSMenu()
        let restore = NSMenuItem(
            title: DeskhubClient.string(DHStrTrayRestore),
            action: #selector(restoreClicked),
            keyEquivalent: ""
        )
        restore.target = self
        menu.addItem(restore)
        let exitItem = NSMenuItem(
            title: DeskhubClient.string(DHStrTrayExit),
            action: #selector(exitClicked),
            keyEquivalent: ""
        )
        exitItem.target = self
        menu.addItem(exitItem)
        menu.popUp(positioning: nil, at: NSPoint(x: 0, y: sender.bounds.height + 2), in: sender)
    }

    @objc private func restoreClicked() {
        onRestore?()
    }

    @objc private func exitClicked() {
        onExit?()
    }
}
