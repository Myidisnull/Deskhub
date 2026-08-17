import AppKit
import SwiftUI

struct LogFileItem: Identifiable, Hashable {
    let name: String
    let path: String
    var id: String { path }
}

private let settingsValueWidth: CGFloat = 120

struct SettingsPage: View {
    @Bindable var agent: AgentModel
    @State private var logFiles: [LogFileItem] = []
    @State private var selectedLogPath = ""
    @State private var logContent = ""
    @State private var showLogDirError = false
    @State private var defaultLogDirHint = ""
    @State private var copiedSessionKey = false

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            deskhubHeading(DeskhubClient.string(DHStrSettingsHeading))
            deskhubHint(DeskhubClient.string(DHStrSettingsHint))

            deskhubSection(DeskhubClient.string(DHStrSettingsSectionLanguage))
            Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 10) {
                GridRow {
                    Text(DeskhubClient.string(DHStrLanguageLabel))
                    Picker("", selection: $agent.language) {
                        ForEach(AppLanguage.allCases) { option in
                            Text(option.label).tag(option)
                        }
                    }
                    .labelsHidden()
                    .frame(width: settingsValueWidth + 40)
                }
            }
            deskhubHint(DeskhubClient.string(DHStrLanguageRestartHint))
                .onChange(of: agent.language) { _, _ in
                    agent.save()
                }

            deskhubSection(DeskhubClient.string(DHStrSettingsSectionVideo))
            Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 10) {
                GridRow {
                    Text("FPS")
                    TextField("", value: $agent.fps, format: .number)
                        .textFieldStyle(.roundedBorder).frame(width: settingsValueWidth)
                }
                GridRow {
                    Text("Bitrate (Mbps)")
                    TextField("", value: $agent.bitrateMbps, format: .number)
                        .textFieldStyle(.roundedBorder).frame(width: settingsValueWidth)
                }
                GridRow {
                    Text("Quality")
                    Picker("", selection: $agent.maxDim) {
                        ForEach(DeskhubAgent.qualityPresets) { preset in
                            Text(preset.label).tag(preset.maxDim)
                        }
                    }
                    .labelsHidden()
                    .frame(width: settingsValueWidth)
                }
            }

            deskhubSection(DeskhubClient.string(DHStrSettingsSectionConnection))
            Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 10) {
                GridRow {
                    Text("UDP port")
                    TextField("", value: $agent.port, format: .number.grouping(.never))
                        .textFieldStyle(.roundedBorder).frame(width: settingsValueWidth)
                }
            }

            deskhubSection(DeskhubClient.string(DHStrSettingsSectionSecurity))
            Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 10) {
                GridRow {
                    Text(DeskhubClient.string(DHStrPasscodeLabel))
                    PasscodeField(passcode: $agent.passcode, width: settingsValueWidth)
                }
            }
            Toggle(DeskhubClient.string(DHStrAllowControlLabel), isOn: $agent.allowInput)
                .toggleStyle(.checkbox)

            deskhubSection(DeskhubClient.string(DHStrSettingsSectionSession))
            Toggle(DeskhubClient.string(DHStrClipboardSyncLabel), isOn: $agent.clipboardSync)
                .toggleStyle(.checkbox)
            Toggle(DeskhubClient.string(DHStrEncryptSessionLabel), isOn: $agent.encryptSession)
                .toggleStyle(.checkbox)
                .onChange(of: agent.encryptSession) { _, _ in agent.save() }
            deskhubHint(DeskhubClient.string(DHStrEncryptSessionHint))
            if agent.encryptSession {
                Toggle(DeskhubClient.string(DHStrEscrowSessionKeyLabel), isOn: $agent.escrowSessionKey)
                    .toggleStyle(.checkbox)
                    .onChange(of: agent.escrowSessionKey) { _, _ in agent.save() }
                deskhubHint(DeskhubClient.string(DHStrEscrowSessionKeyHint))
                Picker(DeskhubClient.string(DHStrSessionKeyLifetimeLabel), selection: $agent.sessionKeyLifetime) {
                    Text(DeskhubClient.string(DHStrSessionKeyLifetimePerShare)).tag(0)
                    Text(DeskhubClient.string(DHStrSessionKeyLifetimePersistent)).tag(1)
                }
                .onChange(of: agent.sessionKeyLifetime) { _, _ in agent.save() }
                Text(DeskhubClient.string(DHStrSessionKeyLabel))
                Text(agent.sessionKeyHex.isEmpty ? "—" : agent.sessionKeyHex)
                    .font(.system(.body, design: .monospaced))
                    .textSelection(.enabled)
                deskhubHint(DeskhubClient.string(DHStrSessionKeyHint))
                HStack {
                    Button(
                        copiedSessionKey
                            ? DeskhubClient.string(DHStrCopied)
                            : DeskhubClient.string(DHStrCopySessionKey)
                    ) {
                        NSPasteboard.general.clearContents()
                        NSPasteboard.general.setString(agent.sessionKeyHex, forType: .string)
                        copiedSessionKey = true
                    }
                    .disabled(agent.sessionKeyHex.isEmpty)
                    Button(DeskhubClient.string(DHStrRefreshSessionKey)) {
                        agent.refreshSessionKey()
                    }
                }
                .task(id: copiedSessionKey) {
                    guard copiedSessionKey else { return }
                    try? await Task.sleep(for: .seconds(1.5))
                    copiedSessionKey = false
                }
            }

            PermissionsSection(agent: agent)

            deskhubSection(DeskhubClient.string(DHStrSettingsSectionLaunch))
            Toggle(DeskhubClient.string(DHStrShareOnLaunchLabel), isOn: $agent.autoShare)
                .toggleStyle(.checkbox)
            Toggle(DeskhubClient.string(DHStrAutostartLabel), isOn: $agent.autostart)
                .toggleStyle(.checkbox)
            Toggle(
                DeskhubClient.string(DHStrRunInBackgroundLabel),
                isOn: Binding(
                    get: { agent.runInBackground },
                    set: { agent.recordRunInBackground($0) }
                )
            )
            if agent.runInBackground {
                Toggle(
                    DeskhubClient.string(DHStrHideTrayIconLabel),
                    isOn: Binding(
                        get: { agent.hideTrayIcon },
                        set: { agent.recordHideTrayIcon($0) }
                    )
                )
            }

            deskhubSection("Logs")
            Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 10) {
                GridRow {
                    Text(DeskhubClient.string(DHStrLogMaxFileMbLabel))
                    TextField("", value: $agent.logMaxFileMb, format: .number)
                        .textFieldStyle(.roundedBorder).frame(width: settingsValueWidth)
                }
                GridRow {
                    Text(DeskhubClient.string(DHStrLogCompressAfterDaysLabel))
                    TextField("", value: $agent.logCompressAfterDays, format: .number)
                        .textFieldStyle(.roundedBorder).frame(width: settingsValueWidth)
                }
                GridRow {
                    Text(DeskhubClient.string(DHStrLogDeleteAfterDaysLabel))
                    TextField("", value: $agent.logDeleteAfterDays, format: .number)
                        .textFieldStyle(.roundedBorder).frame(width: settingsValueWidth)
                }
            }
            deskhubHint(DeskhubClient.string(DHStrLogDirHint))
            HStack(spacing: 8) {
                Text(DeskhubClient.string(DHStrLogDirLabel))
                TextField(defaultLogDirHint, text: $agent.logDir)
                    .textFieldStyle(.roundedBorder)
                    .frame(minWidth: 260)
                Button(DeskhubClient.string(DHStrLogDirBrowse)) { browseLogDir() }
            }

            deskhubHint(DeskhubClient.string(DHStrLogDetailsLabel))
            HStack(spacing: 8) {
                Picker("", selection: $selectedLogPath) {
                    if logFiles.isEmpty {
                        Text(DeskhubClient.string(DHStrLogEmpty)).tag("")
                    } else {
                        ForEach(logFiles) { file in
                            Text(file.name).tag(file.path)
                        }
                    }
                }
                .labelsHidden()
                Button(DeskhubClient.string(DHStrLogRefresh)) { refreshLogView() }
                Button(DeskhubClient.string(DHStrLogOpenFolder)) { _ = dh_log_open_folder() }
            }
            ScrollView {
                Text(logContent.isEmpty ? DeskhubClient.string(DHStrLogEmpty) : logContent)
                    .font(.system(.caption, design: .monospaced))
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .textSelection(.enabled)
            }
            .frame(minHeight: 140, idealHeight: 180, maxHeight: 240)
            .padding(8)
            .background(Color(nsColor: .textBackgroundColor))
            .overlay(
                RoundedRectangle(cornerRadius: 4)
                    .stroke(Color(nsColor: .separatorColor))
            )
        }
        .onAppear { refreshLogView() }
        .onChange(of: selectedLogPath) { _, _ in loadSelectedLog() }
        .onChange(of: agent.fps) { _, _ in agent.save() }
        .onChange(of: agent.bitrateMbps) { _, _ in agent.save() }
        .onChange(of: agent.maxDim) { _, _ in agent.save() }
        .onChange(of: agent.port) { _, _ in agent.save() }
        .onChange(of: agent.passcode) { _, _ in agent.save() }
        .onChange(of: agent.allowInput) { _, _ in agent.save() }
        .onChange(of: agent.autoShare) { _, _ in agent.save() }
        .onChange(of: agent.autostart) { _, _ in agent.applyAutostart() }
        .onChange(of: agent.clipboardSync) { _, _ in agent.save() }
        .onChange(of: agent.encryptSession) { _, _ in agent.save() }
        .onChange(of: agent.logDir) { _, _ in saveLogDir() }
        .onChange(of: agent.logMaxFileMb) { _, _ in agent.save() }
        .onChange(of: agent.logCompressAfterDays) { _, _ in agent.save() }
        .onChange(of: agent.logDeleteAfterDays) { _, _ in agent.save() }
        .alert(
            DeskhubClient.string(DHStrAppTitle),
            isPresented: $showLogDirError
        ) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(DeskhubClient.string(DHStrLogDirInvalid))
        }
    }

    private func saveLogDir() {
        let trimmed = agent.logDir.trimmingCharacters(in: .whitespacesAndNewlines)
        if !trimmed.isEmpty && !dh_log_dir_usable(trimmed) {
            showLogDirError = true
            agent.logDir = DeskhubClient.cString(dh_settings_load().logDir)
            return
        }
        agent.save()
        refreshLogView()
    }

    private func browseLogDir() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = false
        panel.canChooseDirectories = true
        panel.canCreateDirectories = true
        panel.allowsMultipleSelection = false
        let hint = defaultLogDirHint.isEmpty
            ? DeskhubClient.buffered(1024) { dh_default_log_dir($0, $1) }
            : defaultLogDirHint
        panel.directoryURL = URL(fileURLWithPath: hint)
        guard panel.runModal() == .OK, let url = panel.url else { return }
        agent.logDir = url.path
        saveLogDir()
    }

    private func refreshLogView() {
        if defaultLogDirHint.isEmpty {
            defaultLogDirHint = DeskhubClient.buffered(1024) {
                dh_default_log_dir($0, $1)
            }
        }
        let keep = selectedLogPath
        logFiles = DeskhubClient.ffiList(
            64, DHLogFile(),
            { dh_log_files($0, $1) },
            { raw in
                LogFileItem(
                    name: DeskhubClient.cString(raw.name),
                    path: DeskhubClient.cString(raw.path)
                )
            }
        )
        if logFiles.isEmpty {
            selectedLogPath = ""
            logContent = ""
            return
        }
        if let match = logFiles.first(where: { $0.path == keep }) {
            selectedLogPath = match.path
        } else {
            selectedLogPath = logFiles[0].path
        }
        loadSelectedLog()
    }

    private func loadSelectedLog() {
        guard !selectedLogPath.isEmpty else {
            logContent = ""
            return
        }
        let capacity = 512 * 1024
        var buf = [CChar](repeating: 0, count: capacity)
        let n = buf.withUnsafeMutableBufferPointer { ptr in
            dh_log_read(selectedLogPath, ptr.baseAddress, Int32(ptr.count))
        }
        logContent = n > 0 ? String(cString: buf) : ""
    }
}
