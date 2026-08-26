import AppKit
import SwiftUI

struct PermissionsSection: View {
    @Bindable var agent: AgentModel

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Permissions")
                .font(.system(size: 15, weight: .bold))
                .foregroundStyle(DeskhubPalette.heading)
                .padding(.top, 8)
                .frame(maxWidth: .infinity, alignment: .leading)

            PermissionRow(
                title: "Screen Recording",
                granted: agent.hasScreenRecording,
                detail: "Needed to share this Mac's display.",
                grant: { agent.requestScreenRecording() },
                openSettings: DeskhubAgent.openScreenRecordingSettings
            )

            if agent.screenRecordingNeedsRelaunch {
                Text("\(DeskhubClient.string(DHStrAppTitle)) is now listed under Privacy & Security \u{2192} Screen & System "
                    + "Audio Recording. Turn it on there, then quit and reopen \(DeskhubClient.string(DHStrAppTitle)).")
                    .foregroundStyle(DeskhubPalette.muted)
                    .fixedSize(horizontal: false, vertical: true)
            }

            PermissionRow(
                title: "Accessibility",
                granted: agent.hasAccessibility,
                detail: "Needed for the other machine to move the mouse and type.",
                grant: { agent.requestAccessibility() },
                openSettings: DeskhubAgent.openAccessibilitySettings
            )
        }
        .onReceive(
            NotificationCenter.default.publisher(for: NSApplication.didBecomeActiveNotification)
        ) { _ in
            agent.refreshPermissions()
        }
    }
}

private struct PermissionRow: View {
    let title: String
    let granted: Bool
    let detail: String
    let grant: () -> Void
    let openSettings: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 10) {
                Text(title).frame(width: 140, alignment: .leading)
                Text(granted ? "Granted" : "Not granted")
                    .fontWeight(.bold)
                    .foregroundStyle(granted ? DeskhubPalette.online : Color.red)
                Spacer(minLength: 0)
                if !granted {
                    Button("Grant", action: grant)
                }
                Button(DeskhubClient.string(DHStrOpenSystemSettingsLabel), action: openSettings)
            }
            Text(detail)
                .foregroundStyle(DeskhubPalette.muted)
                .fixedSize(horizontal: false, vertical: true)
        }
    }
}
