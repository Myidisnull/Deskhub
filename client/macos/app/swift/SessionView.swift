// =============================================================================
// SessionView.swift — cửa sổ quản lý phiên đang chia sẻ (vai HOST).
//                     Đối ứng client/windows/ui/SessionWindow.
//
// BA VIỆC, ĐÚNG NHƯ BẢN WINDOWS
//   1. Hiện ĐỊA CHỈ + CỔNG để người dùng đọc cho máy bên kia. Đây là lý do màn hình
//      này tồn tại — không có nó thì không ai kết nối vào được.
//   2. Hiện từng nguồn đang chia sẻ kèm số liệu sống (fps/kbps, đã có người xem chưa).
//   3. Thêm/bớt nguồn GIỮA PHIÊN mà không phải dừng rồi share lại.
//
// Số liệu cập nhật 500ms/lần từ AgentModel.poll(); tầng C++ chốt cửa sổ thống kê
// mỗi giây nên hai nhịp poll liền nhau có thể ra cùng một con số — bình thường.
// =============================================================================
import AppKit
import SwiftUI

struct SessionView: View {
    @Binding var route: Route
    @Bindable var model: AgentModel
    @State private var showAddSheet = false

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            header
            Divider()
            addressPanel
            Divider()
            sourceList
            Divider()
            footer
        }
        .sheet(isPresented: $showAddSheet) {
            AddSourceSheet(model: model, isPresented: $showAddSheet)
        }
    }

    private var header: some View {
        HStack {
            Circle()
                .fill(.green)
                .frame(width: 8, height: 8)
            Text(model.statusLine.isEmpty ? "Sharing" : model.statusLine)
                .font(.headline)
            Spacer()
        }
        .padding()
    }

    // Nhiều địa chỉ chứ không một: máy nào cũng có vài đường ra (Wi-Fi, Ethernet,
    // Tailscale) và chỉ người dùng mới biết máy kia nằm ở nhánh nào — xem
    // net/NetInfo.h.
    private var addressPanel: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text("On the other machine, enter one of these:")
                .font(.caption)
                .foregroundStyle(.secondary)
            ForEach(model.addresses, id: \.self) { addr in
                HStack {
                    Text(addressWithPort(addr))
                        .font(.system(.body, design: .monospaced))
                        .textSelection(.enabled)
                    Spacer()
                    Button {
                        copyToPasteboard(addressWithPort(addr))
                    } label: {
                        Image(systemName: "doc.on.doc")
                    }
                    .buttonStyle(.borderless)
                    .help("Copy")
                }
            }
            if model.addresses.isEmpty {
                Text("No network interface found — check your Wi-Fi or Ethernet connection.")
                    .font(.caption)
                    .foregroundStyle(.orange)
            }
        }
        .padding()
    }

    private var sourceList: some View {
        List(model.rows) { row in
            HStack(spacing: 10) {
                Image(systemName: row.starting ? "clock" : "record.circle")
                    .foregroundStyle(row.viewerConnected ? .green : .secondary)

                VStack(alignment: .leading, spacing: 2) {
                    Text(row.name).lineLimit(1)
                    Text(detail(for: row))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }

                Spacer()

                Button("Stop") { model.removeSource(id: row.id) }
                    .controlSize(.small)
                    .disabled(row.starting)
            }
        }
        .frame(maxHeight: .infinity)
    }

    private var footer: some View {
        HStack {
            Button("Add source…") {
                Task {
                    await model.scanSources()
                    showAddSheet = true
                }
            }
            Spacer()
            Button("Stop sharing", role: .destructive) {
                model.stopSharing()
                route = .home
            }
            .buttonStyle(.borderedProminent)
        }
        .padding()
    }

    private func detail(for row: AgentSourceStatus) -> String {
        if row.starting { return "starting…" }
        let size = "\(row.width)×\(row.height)"
        let viewer = row.viewerConnected ? "viewer connected" : "waiting for viewer"
        let rate = String(format: "capture %.0f fps · send %.0f fps, %.0f kbps",
                          row.captureFps, row.sendFps, row.sendKbps)
        return "\(size) · \(viewer) · \(rate)"
    }

    // Địa chỉ từ C++ có dạng "192.168.1.5  (Wi-Fi (en0))" — chèn cổng ngay sau IP để
    // người dùng đọc được nguyên chuỗi cho máy kia gõ.
    private func addressWithPort(_ addr: String) -> String {
        let parts = addr.split(separator: " ", maxSplits: 1, omittingEmptySubsequences: true)
        guard let ip = parts.first else { return addr }
        let rest = parts.count > 1 ? "  \(parts[1])" : ""
        return "\(ip):\(model.activePort)\(rest)"
    }

    private func copyToPasteboard(_ text: String) {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(text, forType: .string)
    }
}

// Chọn thêm nguồn giữa phiên. Danh sách đã được model.scanSources() làm mới trước
// khi sheet mở, nên nó luôn phản ánh những cửa sổ đang thật sự mở.
struct AddSourceSheet: View {
    @Bindable var model: AgentModel
    @Binding var isPresented: Bool

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text("Add a source")
                    .font(.headline)
                Spacer()
                if model.isScanning { ProgressView().controlSize(.small) }
            }
            .padding()

            Divider()

            List(model.available) { source in
                Button {
                    model.addSource(source)
                    isPresented = false
                } label: {
                    HStack {
                        Image(systemName: source.isDisplay ? "display" : "macwindow")
                            .foregroundStyle(.secondary)
                        Text(source.name).lineLimit(1)
                        Spacer()
                        Text("\(source.width)×\(source.height)")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
            }

            Divider()

            HStack {
                Spacer()
                Button("Cancel") { isPresented = false }
            }
            .padding()
        }
        .frame(width: 520, height: 420)
    }
}
