// =============================================================================
// AgentModel.swift — trạng thái vai HOST (chia sẻ máy này) cho SwiftUI.
//
// Không có bản đối ứng bên iOS/Android — hai nền tảng đó không host được
// (docs/11 §3). Bản gần nhất là cửa sổ phiên Win32 ở
// client/windows/ui/SessionWindow.cpp, nhưng ở đây nó là màn hình SwiftUI và mọi
// lệnh đi qua facade thay vì hộp thư của cửa sổ.
//
// VÌ SAO CÓ CẢ startError
//   dha_start thất bại vì nhiều lý do rất khác nhau (cổng bận, thiếu quyền, nguồn
//   không lên hình) và tất cả đều chỉ hiện ra trong log. Người dùng bấm Start rồi
//   không thấy gì xảy ra là trải nghiệm tệ nhất có thể — nên model giữ một dòng lý
//   do để ShareView hiện lên.
// =============================================================================
import Foundation
import Observation

@MainActor @Observable
final class AgentModel {
    // Tuỳ chọn phiên, nhớ lại cho lần sau.
    var port: String = UserDefaults.standard.string(forKey: "sharePort") ?? "47777"
    var fps: Int = UserDefaults.standard.object(forKey: "shareFps") as? Int ?? 60
    var bitrateMbps: Int = UserDefaults.standard.object(forKey: "shareBitrate") as? Int ?? 20
    var allowInput: Bool = UserDefaults.standard.object(forKey: "shareAllowInput") as? Bool ?? true
    // GĐ9: clipboard MẶC ĐỊNH TẮT — hay chứa mật khẩu/OTP, người dùng phải tự bật.
    var shareClipboard: Bool = UserDefaults.standard.object(forKey: "shareClipboard") as? Bool ?? false

    // Danh sách nguồn chia sẻ được + lựa chọn của người dùng.
    var available: [ShareSource] = []
    var selected: Set<String> = []
    var isScanning = false

    // Trạng thái phiên đang chạy.
    var isSharing = false
    var isStarting = false
    var startError = ""
    var statusLine = ""
    var rows: [AgentSourceStatus] = []
    var addresses: [String] = []
    var activePort: UInt16 = 0

    // Đếm nhịp poll. ShareView vẽ một biểu đồ nhịp gửi và cần MỘT MẪU MỖI NHỊP, kể cả
    // khi con số không đổi — theo dõi chính con số đó thì đường biểu đồ đứng hình đúng
    // lúc luồng chạy ổn định nhất, tức là nói ngược hẳn sự thật.
    var tick: UInt64 = 0

    // Quyền. Đọc lại mỗi lần vào màn hình vì người dùng có thể vừa bật trong
    // System Settings mà không khởi động lại app.
    var hasScreenRecording = false
    var hasAccessibility = false

    private var pollTimer: Timer?

    // MARK: - Quyền

    func refreshPermissions() {
        hasScreenRecording = DeskhubAgent.hasScreenRecording
        hasAccessibility = DeskhubAgent.hasAccessibility
    }

    func requestScreenRecording() {
        DeskhubAgent.requestScreenRecording()
        refreshPermissions()
    }

    func requestAccessibility() {
        DeskhubAgent.requestAccessibility()
        refreshPermissions()
    }

    // MARK: - Quét nguồn

    func scanSources() async {
        isScanning = true
        let found = await Task.detached { DeskhubAgent.listShareSources() }.value
        available = found
        // Bỏ khỏi lựa chọn những nguồn đã biến mất giữa hai lần quét (cửa sổ bị đóng).
        let ids = Set(found.map(\.id))
        selected = selected.intersection(ids)
        isScanning = false
        refreshPermissions()
    }

    // MARK: - Vòng đời phiên

    func startSharing() async {
        let picked = available.filter { selected.contains($0.id) }
        guard !picked.isEmpty else { return }

        isStarting = true
        startError = ""
        UserDefaults.standard.set(port, forKey: "sharePort")
        UserDefaults.standard.set(fps, forKey: "shareFps")
        UserDefaults.standard.set(bitrateMbps, forKey: "shareBitrate")
        UserDefaults.standard.set(allowInput, forKey: "shareAllowInput")
        UserDefaults.standard.set(shareClipboard, forKey: "shareClipboard")

        let portNum = UInt16(port) ?? 47777
        let fpsNum = UInt32(fps)
        let bitrateNum = UInt32(bitrateMbps)
        let input = allowInput
        let clipboard = shareClipboard

        let ok = await Task.detached {
            DeskhubAgent.start(
                sources: picked,
                port: portNum,
                fps: fpsNum,
                bitrateMbps: bitrateNum,
                allowInput: input,
                shareClipboard: clipboard
            )
        }.value

        isStarting = false
        isSharing = ok
        if ok {
            activePort = DeskhubAgent.port
            addresses = DeskhubAgent.localAddresses()
            startPolling()
        } else {
            startError = hasScreenRecording
                ? "Could not start sharing. The window may have closed, or the port is in use."
                : "Screen Recording permission is required. Grant it in System Settings, "
                + "then quit and reopen Deskhub."
        }
    }

    func stopSharing() {
        stopPolling()
        DeskhubAgent.stop()
        isSharing = false
        rows = []
        statusLine = ""
        activePort = 0
    }

    // Thêm nguồn giữa phiên. Nó chỉ đặt lệnh — thread Recv thi hành ở vòng kế tiếp,
    // nên hàng "starting…" sẽ xuất hiện ở nhịp poll sau chứ không ngay lập tức.
    func addSource(_ source: ShareSource) {
        DeskhubAgent.addSource(source)
    }

    func removeSource(id: UInt8) {
        DeskhubAgent.removeSource(id: id)
    }

    // MARK: - Hỏi vòng trạng thái

    private func startPolling() {
        stopPolling()
        pollTimer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.poll() }
        }
        poll()
    }

    private func stopPolling() {
        pollTimer?.invalidate()
        pollTimer = nil
    }

    private func poll() {
        statusLine = DeskhubAgent.statusLine()
        rows = DeskhubAgent.status()
        activePort = DeskhubAgent.port
        tick &+= 1
        // Thread Recv có thể tự dừng (lỗi socket) — UI phải theo, không thì người
        // dùng ngồi nhìn một màn hình phiên đã chết.
        if !DeskhubAgent.isRunning {
            isSharing = false
            stopPolling()
        }
    }
}
