// =============================================================================
// ConnectView.swift — màn chính: hộp Host mode + hộp Client mode + nút Exit.
//                     Chép bố cục MainMenuWindow.cpp của bản Windows, từng dòng chữ.
//
// HAI HỘP, MỘT MÀN — giống hệt Windows:
//   • "Host mode"   — chia sẻ máy này: địa chỉ IP (kèm Copy), FPS/Bitrate, nút Share.
//   • "Client mode" — kết nối máy khác: ô IP, nút Connect.
//   KHÔNG có ô Port và KHÔNG có ô "View only": cổng luôn là 47777 và chuột/bàn phím
//   luôn được chia sẻ (chốt 2026-07-27) — cả hai chỉ còn là chữ, không phải lựa chọn.
//
// ĐƯỜNG RẼ HAI VAI:
//   Share   → chia sẻ HẾT màn hình (không có bước chọn nguồn) → SharingSessionView
//   Connect → hỏi host → SourcePickerView (nếu >1 nguồn) → mở MỖI NGUỒN MỘT CỬA SỔ
//             XEM rồi ẩn cửa sổ chính (đối ứng ShowWindow(SW_HIDE) + RunViewer)
//
// QUYỀN macOS: Windows bung UAC lúc bấm Share (admin cho firewall + UIPI); bản mac
// tương ứng là Screen Recording — thiếu thì báo bằng alert ngay lúc bấm, vì macOS
// không tự hiện lỗi (xem cpp/Permissions.h).
// =============================================================================
import AppKit
import SwiftUI

struct MainMenuView: View {
    @Binding var route: Route
    @Bindable var session: SessionModel
    @Bindable var agent: AgentModel

    @State private var shareAlert = ""
    @State private var accessibilityWarning = false
    @Environment(\.openWindow) private var openWindow
    @Environment(\.dismissWindow) private var dismissWindow

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            GroupBox("Host mode - share an application on THIS machine") {
                hostBox.padding(6)
            }
            GroupBox("Client mode - connect to ANOTHER machine") {
                clientBox.padding(6)
            }
            Button("Exit") { NSApplication.shared.terminate(nil) }
                .padding(.top, 4)
        }
        .padding(12)
        .task {
            agent.refreshPermissions()
            agent.loadAddresses()
        }
        .alert("Deskhub", isPresented: showingShareAlert) {
            if !agent.hasScreenRecording {
                Button("Open System Settings") { DeskhubAgent.openScreenRecordingSettings() }
            }
            Button("OK", role: .cancel) {}
        } message: {
            Text(shareAlert)
        }
        // Đối ứng MessageBox cảnh báo của DoShare bên Windows (thiếu admin → input
        // không tới app elevated): thiếu Accessibility thì cảnh báo rồi VẪN chia sẻ
        // — bên kia xem được nhưng không điều khiển được cho tới khi quyền được cấp.
        .alert("Deskhub", isPresented: $accessibilityWarning) {
            Button("Share anyway") { Task { await doShare() } }
            Button("Open System Settings", role: .cancel) {
                DeskhubAgent.openAccessibilitySettings()
            }
        } message: {
            Text("Mouse and keyboard are always shared, but macOS silently drops "
                + "them until Deskhub has Accessibility permission. The other "
                + "machine will see this Mac but not control it.")
        }
    }

    // MARK: - Hộp host

    private var hostBox: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Others connect to you using one of these IP addresses:")

            if agent.addresses.isEmpty {
                Text("(no network address found)").foregroundStyle(.secondary)
            } else {
                // Giá trị Copy là IP TRẦN — đúng thứ phải gõ vào ô địa chỉ phía kia.
                ForEach(agent.addresses) { addr in
                    HStack(spacing: 8) {
                        Text(addr.name)
                            .frame(width: 150, alignment: .leading)
                            .lineLimit(1)
                        Text(addr.ip).font(.system(.body, design: .monospaced))
                            .textSelection(.enabled)
                        Spacer()
                        Button("Copy") {
                            NSPasteboard.general.clearContents()
                            NSPasteboard.general.setString(addr.ip, forType: .string)
                        }
                    }
                }
            }

            // Không có ô Port: cổng là hằng số 47777 (net/UdpSocket.h). Chỉ nói ra
            // cho người dùng biết con số đó, vì firewall có thể cần mở tay.
            HStack(spacing: 8) {
                Text("UDP port 47777")
                Spacer().frame(width: 8)
                Text("FPS")
                TextField("60", value: $agent.fps, format: .number)
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 52)
                Text("Bitrate (Mbps)")
                TextField("20", value: $agent.bitrateMbps, format: .number)
                    .textFieldStyle(.roundedBorder)
                    .frame(width: 52)
                // Màn Mac là Retina, nên "Native" gần như luôn là lựa chọn TỆ trên
                // một đường truyền thật — 3024×1964 ở 20 Mbps mờ hơn hẳn 1920×1246
                // ở cùng bitrate. Để nó trong danh sách cho ai có LAN 10Gb và màn
                // 5K, nhưng mặc định là 1080p. Lý do đầy đủ ở AgentOptions::maxDim.
                Picker("Quality", selection: $agent.maxDim) {
                    Text("720p").tag(1280)
                    Text("1080p").tag(1920)
                    Text("1440p").tag(2560)
                    Text("Native").tag(0)
                }
                .frame(width: 150)
            }

            Button {
                Task { await share() }
            } label: {
                if agent.isStarting {
                    ProgressView().controlSize(.small).frame(maxWidth: .infinity)
                } else {
                    Text("Share...  (pick the display to share)")
                        .frame(maxWidth: .infinity)
                }
            }
            .disabled(agent.isStarting)
        }
    }

    // MARK: - Hộp client

    private var clientBox: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Host machine IP address:")
            HStack(spacing: 8) {
                TextField("", text: $session.address)
                    .textFieldStyle(.roundedBorder)
                    .onSubmit(connect)
                    .disabled(session.isConnecting)
                Button("Connect", action: connect)
                    .buttonStyle(.borderedProminent)
                    .disabled(session.address.isEmpty || session.isConnecting)
            }
            if session.isConnecting {
                HStack(spacing: 8) {
                    ProgressView().controlSize(.small)
                    Text("Asking the host what it is sharing…")
                        .foregroundStyle(.secondary)
                }
            }
            if !session.connectError.isEmpty {
                Text(session.connectError)
                    .foregroundStyle(.red)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }

    // MARK: - Hành động

    private var showingShareAlert: Binding<Bool> {
        Binding(get: { !shareAlert.isEmpty }, set: { if !$0 { shareAlert = "" } })
    }

    // Giống DoShare bên Windows: không có bước chọn nguồn — bấm Share là chia sẻ HẾT
    // màn hình đang gắn, danh sách chốt tại đó. Windows cảnh báo thiếu admin bằng
    // MessageBox rồi vẫn tiếp tục; bản mac làm y vậy với hai quyền của nó: thiếu
    // Screen Recording thì CHẶN (không có nó capture chắc chắn hỏng), thiếu
    // Accessibility thì cảnh báo rồi cho chia sẻ tiếp.
    private func share() async {
        agent.refreshPermissions()
        if !agent.hasScreenRecording {
            shareAlert = "Screen Recording permission is required. Grant it in "
                + "System Settings, then quit and reopen Deskhub."
            return
        }
        if !agent.hasAccessibility {
            accessibilityWarning = true
            return
        }
        await doShare()
    }

    private func doShare() async {
        if await agent.startSharing() {
            route = .sharing
        } else {
            shareAlert = agent.startError
        }
    }

    // Danh sách rỗng gộp hai trường hợp — host im lặng (bản cũ / mất gói) và host
    // không chia sẻ gì — thành một: cứ vào NGUỒN 0 và để ClientSession báo lỗi thật.
    // Một nguồn thì bỏ qua luôn hộp chọn (Windows cũng vậy — SourcePickerDialog trả
    // thẳng khi chỉ có một nguồn).
    private func connect() {
        guard !session.address.isEmpty, !session.isConnecting else { return }
        Task {
            let sources = await session.listSources()
            if sources.count > 1 {
                route = .sourcePicker(sources)
            } else {
                openViewers(sources, address: session.address,
                            openWindow: openWindow, dismissWindow: dismissWindow)
            }
        }
    }
}

// Mở mỗi nguồn một cửa sổ xem rồi ẩn cửa sổ chính — đối ứng RunViewer + SW_HIDE.
// Danh sách rỗng = host im lặng: vẫn mở một cửa sổ cho nguồn 0 (RunViewer cũng vậy).
// Dùng chung cho MainMenuView (≤1 nguồn) và SourcePickerView (nút View).
@MainActor
func openViewers(_ picked: [Source], address: String,
                 openWindow: OpenWindowAction, dismissWindow: DismissWindowAction)
{
    if picked.isEmpty {
        openWindow(value: ViewerRequest(address: address, sourceId: 0, name: ""))
    } else {
        for source in picked {
            openWindow(value: ViewerRequest(
                address: address, sourceId: source.id, name: source.name
            ))
        }
    }
    dismissWindow(id: "main")
}
