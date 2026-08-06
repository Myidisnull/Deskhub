import SwiftUI

struct SettingsPage: View {
    @Bindable var agent: AgentModel

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            deskhubHeading(DeskhubClient.string(DHStrSettingsHeading))
            deskhubHint(DeskhubClient.string(DHStrSettingsHint))

            deskhubSection("Video")
            Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 10) {
                GridRow {
                    Text("FPS")
                    TextField("", value: $agent.fps, format: .number)
                        .textFieldStyle(.roundedBorder).frame(width: 90)
                }
                GridRow {
                    Text("Bitrate (Mbps)")
                    TextField("", value: $agent.bitrateMbps, format: .number)
                        .textFieldStyle(.roundedBorder).frame(width: 90)
                }
                GridRow {
                    Text("Quality")
                    Picker("", selection: $agent.maxDim) {
                        ForEach(DeskhubAgent.qualityPresets) { preset in
                            Text(preset.label).tag(preset.maxDim)
                        }
                    }
                    .labelsHidden()
                    .frame(width: 120)
                }
            }

            deskhubSection("Connection")
            Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 10) {
                GridRow {
                    Text("UDP port")
                    TextField("", value: $agent.port, format: .number)
                        .textFieldStyle(.roundedBorder).frame(width: 90)
                }
            }

            deskhubSection("Security")
            Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 10) {
                GridRow {
                    Text(DeskhubClient.string(DHStrPasscodeLabel))
                    TextField("", text: $agent.passcode)
                        .textFieldStyle(.roundedBorder).frame(width: 64)
                }
            }

            PermissionsSection(agent: agent)
        }
        .onChange(of: agent.fps) { _, _ in agent.save() }
        .onChange(of: agent.bitrateMbps) { _, _ in agent.save() }
        .onChange(of: agent.maxDim) { _, _ in agent.save() }
        .onChange(of: agent.port) { _, _ in agent.save() }
        .onChange(of: agent.passcode) { _, _ in agent.save() }
        .onChange(of: agent.allowInput) { _, _ in agent.save() }
    }
}
