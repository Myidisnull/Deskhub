// =============================================================================
// ShareView.swift — chia sẻ máy này (vai HOST): quyền, địa chỉ, danh sách màn hình,
//                   trạng thái phiên — tất cả trên MỘT màn.
//
// GIAO DIỆN TRẦN (2026-07-27)
//   SwiftUI dựng sẵn, không hệ thiết kế riêng, không biểu đồ. Số liệu hiện bằng chữ.
//
// KHÔNG CÓ GÌ ĐỂ CHỌN
//   Bấm "Start sharing" là chia sẻ TẤT CẢ màn hình đang gắn, và danh sách chốt tại
//   đó. Lưới màn hình bên dưới vì thế CHỈ ĐỌC — nó ở đây để người dùng biết chính xác
//   cái gì đang bị nhìn thấy, không phải để tick. (Ô tick sống + nút Add/Stop từng
//   nguồn đã bỏ cùng ngày.)
//
// QUYỀN ĐỨNG TRƯỚC MỌI THỨ KHÁC
//   Thiếu Screen Recording thì danh sách nguồn gần như trống và người dùng không thể
//   đoán vì sao (macOS không báo lỗi — xem cpp/Permissions.h). Nên cảnh báo quyền nằm
//   trên cùng, và nút chia sẻ bị khoá khi chưa có quyền: thà chặn rõ ràng còn hơn để
//   họ bấm rồi nhận một thất bại không giải thích được.
//
// ĐỊA CHỈ LÀ IP TRẦN
//   Cổng luôn 47777 nên người bên kia không phải gõ nó.
// =============================================================================
import SwiftUI

struct ShareView: View {
    @Binding var route: Route
    @Bindable var model: AgentModel

    private let fpsOptions = [30, 60, 120]
    private let bitrateOptions = [8, 20, 40]

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                permissionBanner
                addressSection
                Divider()
                viewerSection
                Divider()
                displaySection
                if !model.startError.isEmpty {
                    Text(model.startError)
                        .foregroundStyle(.red)
                        .fixedSize(horizontal: false, vertical: true)
                }
                Divider()
                controls
            }
            .padding()
            .frame(maxWidth: .infinity, alignment: .leading)
        }
        .task {
            model.refreshPermissions()
            await model.scanSources()
        }
    }

    // MARK: - Quyền

    @ViewBuilder private var permissionBanner: some View {
        if !model.hasScreenRecording {
            VStack(alignment: .leading, spacing: 6) {
                Text("Screen Recording permission is required")
                    .font(.headline)
                Text("Grant it in System Settings, then quit and reopen Deskhub.")
                Button("Open System Settings") {
                    DeskhubAgent.openScreenRecordingSettings()
                }
            }
        } else if !model.hasAccessibility {
            VStack(alignment: .leading, spacing: 6) {
                Text("Accessibility permission is required")
                    .font(.headline)
                Text("Mouse and keyboard are always shared, so without it the other machine can see this Mac but not control it.")
                    .fixedSize(horizontal: false, vertical: true)
                Button("Grant Accessibility") { model.requestAccessibility() }
            }
        }
    }

    // MARK: - Địa chỉ

    private var addressSection: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Enter this on the other machine").font(.headline)
            if !model.isSharing {
                Text("Not sharing yet.").foregroundStyle(.secondary)
            } else if model.addresses.isEmpty {
                Text("No network address found.").foregroundStyle(.orange)
            } else {
                ForEach(model.addresses, id: \.self) { raw in
                    Text(raw).font(.system(.body, design: .monospaced))
                        .textSelection(.enabled)
                }
                Text("UDP port 47777").foregroundStyle(.secondary)
            }
        }
    }

    // MARK: - Máy đang xem

    // Giao thức không mang tên hay địa chỉ của người xem về phía host, nên panel này
    // nói đúng thứ nó biết: CÓ ai đang xem hay không, và luồng đang chạy nhanh cỡ nào.
    private var viewerSection: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("Viewer").font(.headline)
            if watchedRows.isEmpty {
                Text("Nobody is watching.").foregroundStyle(.secondary)
            } else {
                Text("Connected · keys + mouse")
                Text(String(
                    format: "sending %.0f fps · %.1f Mbps · capture %.0f fps",
                    totalSendFps, totalSendKbps / 1000, maxCaptureFps
                ))
                .font(.system(.body, design: .monospaced))
            }
        }
    }

    private var watchedRows: [AgentSourceStatus] {
        model.rows.filter(\.viewerConnected)
    }

    private var totalSendFps: Double { watchedRows.reduce(0) { $0 + $1.sendFps } }
    private var totalSendKbps: Double { watchedRows.reduce(0) { $0 + $1.sendKbps } }
    private var maxCaptureFps: Double { watchedRows.map(\.captureFps).max() ?? 0 }

    // MARK: - Danh sách màn hình (chỉ đọc)

    private var displaySection: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text("Displays").font(.headline)
                if model.isScanning { ProgressView().controlSize(.small) }
                Spacer()
                Button("Refresh") { Task { await model.scanSources() } }
                    .disabled(model.isScanning)
            }

            if model.available.isEmpty {
                Text("No display found.").foregroundStyle(.secondary)
            } else {
                Text("\(model.available.count) display(s) — all shared")
                    .foregroundStyle(.secondary)
                ForEach(model.available) { source in
                    HStack(spacing: 8) {
                        Image(systemName: "display")
                        Text(source.name)
                        Text("\(source.width)×\(source.height)")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                        Spacer()
                        if let live = liveRow(for: source), model.isSharing {
                            Text(live.starting ? "starting" : "streaming")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                    }
                }
            }
        }
    }

    // Nối một nguồn trong danh sách quét với dòng trạng thái của phiên bằng TÊN: tầng
    // C++ không trả sourceId nó vừa cấp, và tên trong AgentSourceStatus chính là chuỗi
    // ta đã gửi xuống, nên nó khớp nguyên văn.
    private func liveRow(for source: ShareSource) -> AgentSourceStatus? {
        model.rows.first { $0.name == source.name }
    }

    // MARK: - Điều khiển

    private var controls: some View {
        HStack(spacing: 12) {
            Picker("fps", selection: $model.fps) {
                ForEach(fpsOptions, id: \.self) { Text("\($0)").tag($0) }
            }
            .frame(width: 110)
            .disabled(model.isSharing)

            Picker("Mbps", selection: $model.bitrateMbps) {
                ForEach(bitrateOptions, id: \.self) { Text("\($0)").tag($0) }
            }
            .frame(width: 120)
            .disabled(model.isSharing)

            Spacer()

            if model.isSharing {
                Button("Stop sharing") { model.stopSharing() }
                    .buttonStyle(.borderedProminent)
            } else {
                Button {
                    Task { await model.startSharing() }
                } label: {
                    if model.isStarting {
                        ProgressView().controlSize(.small)
                    } else {
                        Text("Start sharing")
                    }
                }
                .buttonStyle(.borderedProminent)
                .disabled(model.available.isEmpty || model.isStarting || !model.hasScreenRecording)
            }
        }
    }
}
