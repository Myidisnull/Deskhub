import SwiftUI

struct SettingsPage: View {
    @Bindable var agent: AgentModel

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            deskhubHeading(DeskhubClient.string(DHStrSettingsHeading))
            deskhubHint(DeskhubClient.string(DHStrSettingsHint))

            deskhubSection(DeskhubClient.string(DHStrSettingsSectionVideo))
            Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 10) {
                GridRow {
                    Text(DeskhubClient.string(DHStrFpsLabel))
                    TextField("", value: $agent.fps, format: .number)
                        .textFieldStyle(.roundedBorder).frame(width: 90)
                }
                GridRow {
                    Text(DeskhubClient.string(DHStrBitrateLabel))
                    TextField("", value: $agent.bitrateMbps, format: .number)
                        .textFieldStyle(.roundedBorder).frame(width: 90)
                }
                GridRow {
                    Text(DeskhubClient.string(DHStrQualityLabel))
                    Picker("", selection: $agent.maxDim) {
                        ForEach(DeskhubAgent.qualityPresets) { preset in
                            Text(preset.label).tag(preset.maxDim)
                        }
                    }
                    .labelsHidden()
                    .frame(width: 120)
                }
            }

            deskhubSection(DeskhubClient.string(DHStrSettingsSectionConnection))
            Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 10) {
                GridRow {
                    Text(DeskhubClient.string(DHStrUdpPortLabel))
                    TextField("", value: $agent.port, format: .number.grouping(.never))
                        .textFieldStyle(.roundedBorder).frame(width: 90)
                }
            }

            deskhubSection(DeskhubClient.string(DHStrSettingsSectionSecurity))
            Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 10) {
                GridRow {
                    Text(DeskhubClient.string(DHStrPasscodeLabel))
                    PasscodeField(passcode: $agent.passcode, width: 64)
                }
            }
            deskhubHint(DeskhubClient.string(DHStrPasscodeHint))
            Toggle(DeskhubClient.string(DHStrAllowControlLabel), isOn: $agent.allowInput)
                .toggleStyle(.checkbox)

            deskhubSection(DeskhubClient.string(DHStrSettingsSectionSession))
            Toggle(DeskhubClient.string(DHStrClipboardSyncLabel), isOn: $agent.clipboardSync)
                .toggleStyle(.checkbox)
            Toggle(DeskhubClient.string(DHStrShareAudioLabel), isOn: $agent.shareAudio)
                .toggleStyle(.checkbox)
            Toggle(DeskhubClient.string(DHStrPlayAudioLabel), isOn: $agent.playAudio)
                .toggleStyle(.checkbox)
            Toggle(DeskhubClient.string(DHStrKeepAwakeLabel), isOn: $agent.keepAwake)
                .toggleStyle(.checkbox)

            PermissionsSection(agent: agent)

            deskhubSection(DeskhubClient.string(DHStrSettingsSectionLaunch))
            Toggle(DeskhubClient.string(DHStrAutostartLabel), isOn: $agent.autostart)
                .toggleStyle(.checkbox)
            Toggle(DeskhubClient.string(DHStrAutoShareLabel), isOn: $agent.autoShare)
                .toggleStyle(.checkbox)
            Toggle(DeskhubClient.string(DHStrCloseToTrayLabel), isOn: $agent.startHidden)
                .toggleStyle(.checkbox)
        }
        .onChange(of: agent.fps) { _, _ in agent.save() }
        .onChange(of: agent.bitrateMbps) { _, _ in agent.save() }
        .onChange(of: agent.maxDim) { _, _ in agent.save() }
        .onChange(of: agent.port) { _, _ in agent.save() }
        .onChange(of: agent.passcode) { _, _ in agent.save() }
        .onChange(of: agent.allowInput) { _, _ in agent.save() }
        .onChange(of: agent.autoShare) { _, _ in agent.save() }
        .onChange(of: agent.autostart) { _, _ in agent.applyAutostart() }
        .onChange(of: agent.startHidden) { _, _ in agent.save() }
        .onChange(of: agent.clipboardSync) { _, _ in agent.save() }
        .onChange(of: agent.shareAudio) { _, _ in agent.save() }
        .onChange(of: agent.playAudio) { _, _ in agent.save() }
        .onChange(of: agent.keepAwake) { _, _ in agent.save() }
    }
}
