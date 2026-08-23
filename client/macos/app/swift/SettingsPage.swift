import SwiftUI

struct SettingsPage: View {
    @Bindable var sharing: SharingModel

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            deskhubHeading(DeskhubClient.string(DHStrSettingsHeading))
            deskhubHint(DeskhubClient.string(DHStrSettingsHint))

            deskhubSection(DeskhubClient.string(DHStrSettingsSectionVideo))
            Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 10) {
                GridRow {
                    Text(DeskhubClient.string(DHStrFpsLabel))
                    TextField("", value: $sharing.fps, format: .number)
                        .textFieldStyle(.roundedBorder).frame(width: 90)
                }
                GridRow {
                    Text(DeskhubClient.string(DHStrBitrateLabel))
                    TextField("", value: $sharing.bitrateMbps, format: .number)
                        .textFieldStyle(.roundedBorder).frame(width: 90)
                }
                GridRow {
                    Text(DeskhubClient.string(DHStrQualityLabel))
                    Picker("", selection: $sharing.maxDim) {
                        ForEach(DeskhubShare.qualityPresets) { preset in
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
                    TextField("", value: $sharing.port, format: .number.grouping(.never))
                        .textFieldStyle(.roundedBorder).frame(width: 90)
                }
            }

            deskhubSection(DeskhubClient.string(DHStrSettingsSectionSecurity))
            Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 10) {
                GridRow {
                    Text(DeskhubClient.string(DHStrPasscodeLabel))
                    PasscodeField(passcode: $sharing.passcode, width: 64)
                }
            }
            deskhubHint(DeskhubClient.string(DHStrPasscodeHint))
            Toggle(DeskhubClient.string(DHStrAllowControlLabel), isOn: $sharing.allowInput)
                .toggleStyle(.checkbox)

            deskhubSection(DeskhubClient.string(DHStrSettingsSectionSession))
            Toggle(DeskhubClient.string(DHStrClipboardSyncLabel), isOn: $sharing.clipboardSync)
                .toggleStyle(.checkbox)
            Toggle(DeskhubClient.string(DHStrShareAudioLabel), isOn: $sharing.shareAudio)
                .toggleStyle(.checkbox)
            Toggle(DeskhubClient.string(DHStrPlayAudioLabel), isOn: $sharing.playAudio)
                .toggleStyle(.checkbox)
            Toggle(DeskhubClient.string(DHStrKeepAwakeLabel), isOn: $sharing.keepAwake)
                .toggleStyle(.checkbox)

            PermissionsSection(sharing: sharing)

            deskhubSection(DeskhubClient.string(DHStrSettingsSectionLaunch))
            Toggle(DeskhubClient.string(DHStrAutostartLabel), isOn: $sharing.autostart)
                .toggleStyle(.checkbox)
            Toggle(DeskhubClient.string(DHStrAutoShareLabel), isOn: $sharing.autoShare)
                .toggleStyle(.checkbox)
            Toggle(DeskhubClient.string(DHStrCloseToTrayLabel), isOn: $sharing.startHidden)
                .toggleStyle(.checkbox)
        }
        .onChange(of: sharing.fps) { _, _ in sharing.save() }
        .onChange(of: sharing.bitrateMbps) { _, _ in sharing.save() }
        .onChange(of: sharing.maxDim) { _, _ in sharing.save() }
        .onChange(of: sharing.port) { _, _ in sharing.save() }
        .onChange(of: sharing.passcode) { _, _ in sharing.save() }
        .onChange(of: sharing.allowInput) { _, _ in sharing.save() }
        .onChange(of: sharing.autoShare) { _, _ in sharing.save() }
        .onChange(of: sharing.autostart) { _, _ in sharing.applyAutostart() }
        .onChange(of: sharing.startHidden) { _, _ in sharing.save() }
        .onChange(of: sharing.clipboardSync) { _, _ in sharing.save() }
        .onChange(of: sharing.shareAudio) { _, _ in sharing.save() }
        .onChange(of: sharing.playAudio) { _, _ in sharing.save() }
        .onChange(of: sharing.keepAwake) { _, _ in sharing.save() }
    }
}
