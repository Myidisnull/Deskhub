// =============================================================================
// ShareView.swift — chọn nguồn + tuỳ chọn rồi bắt đầu chia sẻ (vai HOST).
//                   Đối ứng client/windows/ui/WindowPickerDialog + phần tuỳ chọn
//                   của MainMenuWindow.
//
// QUYỀN ĐỨNG TRƯỚC MỌI THỨ KHÁC
//   Thiếu Screen Recording thì danh sách nguồn gần như trống và người dùng không thể
//   đoán vì sao (macOS không báo lỗi — xem agent/Permissions.h). Nên banner quyền
//   nằm trên cùng, và nút Start bị khoá khi chưa có quyền: thà chặn rõ ràng còn hơn
//   để họ bấm Start rồi nhận một thất bại không giải thích được.
// =============================================================================
import SwiftUI

struct ShareView: View {
    @Binding var route: Route
    @Bindable var model: AgentModel

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            header
            Divider()

            if !model.hasScreenRecording {
                permissionBanner(
                    title: "Screen Recording permission required",
                    detail: "macOS needs this to capture your screen. After granting it, "
                        + "quit and reopen Deskhub.",
                    action: "Open System Settings",
                    onAction: { DeskhubAgent.openScreenRecordingSettings() }
                )
            } else if model.allowInput, !model.hasAccessibility {
                permissionBanner(
                    title: "Accessibility permission required for remote control",
                    detail: "Without it macOS silently drops injected input. "
                        + "Sharing still works in view-only mode.",
                    action: "Grant",
                    onAction: { model.requestAccessibility() }
                )
            }

            sourceList
            Divider()
            optionsBar
        }
        .task {
            model.refreshPermissions()
            await model.scanSources()
        }
    }

    private var header: some View {
        HStack {
            Text("Share this Mac")
                .font(.headline)
            Spacer()
            if model.isScanning { ProgressView().controlSize(.small) }
            Button("Refresh") { Task { await model.scanSources() } }
                .disabled(model.isScanning)
            Button("Back") { route = .home }
        }
        .padding()
    }

    private func permissionBanner(
        title: String,
        detail: String,
        action: String,
        onAction: @escaping () -> Void
    ) -> some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: "exclamationmark.triangle.fill")
                .foregroundStyle(.orange)
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(.subheadline.bold())
                Text(detail).font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
            Button(action, action: onAction)
                .controlSize(.small)
        }
        .padding(12)
        .background(.orange.opacity(0.12))
    }

    // Danh sách nguồn với ô tick — chia sẻ nhiều cửa sổ cùng lúc là tính năng GĐ6
    // (một cổng, mỗi nguồn một sessionId; xem Wire.h §SourceInfo).
    private var sourceList: some View {
        List(model.available, selection: Binding(
            get: { model.selected },
            set: { model.selected = $0 }
        )) { source in
            HStack(spacing: 10) {
                Toggle(isOn: Binding(
                    get: { model.selected.contains(source.id) },
                    set: { on in
                        if on { model.selected.insert(source.id) } else { model.selected.remove(source.id) }
                    }
                )) {
                    EmptyView()
                }
                .labelsHidden()

                Image(systemName: source.isDisplay ? "display" : "macwindow")
                    .foregroundStyle(.secondary)

                VStack(alignment: .leading, spacing: 2) {
                    Text(source.name).lineLimit(1)
                    Text("\(source.width)×\(source.height)")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                Spacer()
            }
        }
        .frame(maxHeight: .infinity)
    }

    private var optionsBar: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 16) {
                LabeledContent("Port") {
                    TextField("47777", text: $model.port)
                        .frame(width: 80)
                }
                LabeledContent("FPS") {
                    Picker("", selection: $model.fps) {
                        Text("30").tag(30)
                        Text("60").tag(60)
                    }
                    .labelsHidden()
                    .frame(width: 70)
                }
                LabeledContent("Bitrate") {
                    Picker("", selection: $model.bitrateMbps) {
                        Text("8 Mbps").tag(8)
                        Text("20 Mbps").tag(20)
                        Text("40 Mbps").tag(40)
                    }
                    .labelsHidden()
                    .frame(width: 110)
                }
                Toggle("Allow remote control", isOn: $model.allowInput)
                Spacer()
            }

            if !model.startError.isEmpty {
                Text(model.startError)
                    .font(.caption)
                    .foregroundStyle(.red)
                    .fixedSize(horizontal: false, vertical: true)
            }

            HStack {
                Text(model.selected.isEmpty
                    ? "Select at least one window or display."
                    : "\(model.selected.count) selected")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Spacer()
                Button {
                    Task {
                        await model.startSharing()
                        if model.isSharing { route = .session }
                    }
                } label: {
                    if model.isStarting {
                        ProgressView().controlSize(.small).frame(width: 90)
                    } else {
                        Text("Start sharing").frame(width: 90)
                    }
                }
                .buttonStyle(.borderedProminent)
                .disabled(model.selected.isEmpty || model.isStarting || !model.hasScreenRecording)
            }
        }
        .padding()
    }
}
