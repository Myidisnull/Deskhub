import SwiftUI

struct SettingsView: View {
    private static let portSettle = Duration.milliseconds(600)

    @Bindable var settings: SettingsModel
    let onPortChange: (UInt16) -> Void

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                deskhubHeading(DeskhubClient.string(DHStrClientSettingsHeading))
                deskhubHint(DeskhubClient.string(DHStrClientSettingsHint))

                deskhubSection(DeskhubClient.string(DHStrSettingsSectionLanguage))
                HStack(spacing: 12) {
                    Text(DeskhubClient.string(DHStrLanguageLabel))
                    Spacer(minLength: 0)
                    Picker("", selection: $settings.language) {
                        ForEach(AppLanguage.allCases) { option in
                            Text(option.label).tag(option)
                        }
                    }
                    .labelsHidden()
                    .frame(width: 160)
                }
                deskhubHint(DeskhubClient.string(DHStrLanguageRestartHint))
                    .onChange(of: settings.language) { _, _ in
                        settings.save()
                    }

                deskhubSection(DeskhubClient.string(DHStrSettingsSectionConnection))
                HStack(spacing: 12) {
                    Text("UDP port")
                    Spacer(minLength: 0)
                    TextField("", value: $settings.port, format: .number.grouping(.never))
                        .textFieldStyle(.roundedBorder)
                        .keyboardType(.numberPad)
                        .multilineTextAlignment(.trailing)
                        .frame(width: 110)
                }
                deskhubHint(
                    DeskhubClient.buffered(64) {
                        dh_udp_port_line(UInt32(settings.acceptedPort), $0, $1)
                    }
                )

                deskhubSection(DeskhubClient.string(DHStrSettingsSectionSession))
                Toggle(isOn: $settings.clipboardSync) {
                    Text(DeskhubClient.string(DHStrClipboardSyncLabel))
                }
                .onChange(of: settings.clipboardSync) { _, _ in
                    settings.save()
                }
                Toggle(isOn: $settings.shareAudio) {
                    Text(DeskhubClient.string(DHStrShareAudioLabel))
                }
                .onChange(of: settings.shareAudio) { _, _ in
                    settings.save()
                }
                Toggle(isOn: $settings.playAudio) {
                    Text(DeskhubClient.string(DHStrPlayAudioLabel))
                }
                .onChange(of: settings.playAudio) { _, _ in
                    settings.save()
                }
                Toggle(isOn: $settings.keepAwake) {
                    Text(DeskhubClient.string(DHStrKeepAwakeLabel))
                }
                .onChange(of: settings.keepAwake) { _, _ in
                    settings.save()
                }

                deskhubSection("Logs")
                HStack(spacing: 12) {
                    Text(DeskhubClient.string(DHStrLogMaxFileMbLabel))
                    Spacer(minLength: 0)
                    TextField("", value: $settings.logMaxFileMb, format: .number.grouping(.never))
                        .textFieldStyle(.roundedBorder)
                        .keyboardType(.numberPad)
                        .multilineTextAlignment(.trailing)
                        .frame(width: 110)
                }
                HStack(spacing: 12) {
                    Text(DeskhubClient.string(DHStrLogCompressAfterDaysLabel))
                    Spacer(minLength: 0)
                    TextField(
                        "", value: $settings.logCompressAfterDays, format: .number.grouping(.never)
                    )
                    .textFieldStyle(.roundedBorder)
                    .keyboardType(.numberPad)
                    .multilineTextAlignment(.trailing)
                    .frame(width: 110)
                }
                HStack(spacing: 12) {
                    Text(DeskhubClient.string(DHStrLogDeleteAfterDaysLabel))
                    Spacer(minLength: 0)
                    TextField(
                        "", value: $settings.logDeleteAfterDays, format: .number.grouping(.never)
                    )
                    .textFieldStyle(.roundedBorder)
                    .keyboardType(.numberPad)
                    .multilineTextAlignment(.trailing)
                    .frame(width: 110)
                }

                ProjectFooter()
            }
            .padding()
        }
        .task(id: settings.port) {
            try? await Task.sleep(for: SettingsView.portSettle)
            guard !Task.isCancelled else { return }
            settings.save()
            onPortChange(settings.acceptedPort)
        }
        .task(id: settings.logMaxFileMb) {
            try? await Task.sleep(for: SettingsView.portSettle)
            guard !Task.isCancelled else { return }
            settings.save()
        }
        .task(id: settings.logCompressAfterDays) {
            try? await Task.sleep(for: SettingsView.portSettle)
            guard !Task.isCancelled else { return }
            settings.save()
        }
        .task(id: settings.logDeleteAfterDays) {
            try? await Task.sleep(for: SettingsView.portSettle)
            guard !Task.isCancelled else { return }
            settings.save()
        }
    }
}

struct ProjectFooter: View {
    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            if let url = URL(string: DeskhubClient.string(DHStrProjectUrl)) {
                Link(DeskhubClient.string(DHStrProjectLinkLabel), destination: url)
            }

            Text(DeskhubClient.buffered(64) { dh_version_line($0, $1) })
                .font(.caption)
                .foregroundStyle(DeskhubPalette.muted)
        }
        .padding(.top, 8)
    }
}
