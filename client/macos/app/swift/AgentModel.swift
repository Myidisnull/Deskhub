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
    // Tuỳ chọn phiên, nhớ lại cho lần sau. KHÔNG có `port` và `allowInput`: cổng
    // luôn 47777 và chuột/bàn phím luôn được chia sẻ (chốt 2026-07-27).
    var fps: Int = UserDefaults.standard.object(forKey: "shareFps") as? Int ?? 60
    var bitrateMbps: Int = UserDefaults.standard.object(forKey: "shareBitrate") as? Int ?? 20

    // Danh sách màn hình chia sẻ được. KHÔNG có `selected` (bỏ 2026-07-27): bấm
    // Chia sẻ là chia sẻ HẾT, nên danh sách này vừa là thứ hiển thị vừa là thứ gửi đi.
    var available: [ShareSource] = []
    var isScanning = false

    // Trạng thái phiên đang chạy.
    var isSharing = false
    var isStarting = false
    var startError = ""
    var statusLine = ""
    var rows: [AgentSourceStatus] = []
    var addresses: [String] = []

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
        isScanning = false
        refreshPermissions()
    }

    // MARK: - Vòng đời phiên

    // Chia sẻ TẤT CẢ màn hình quét được. Danh sách chốt ở đây và không đổi trong suốt
    // phiên — cắm thêm màn hình giữa chừng thì phải dừng rồi chia sẻ lại.
    func startSharing() async {
        let picked = available
        guard !picked.isEmpty else { return }

        isStarting = true
        startError = ""
        UserDefaults.standard.set(fps, forKey: "shareFps")
        UserDefaults.standard.set(bitrateMbps, forKey: "shareBitrate")

        let fpsNum = UInt32(fps)
        let bitrateNum = UInt32(bitrateMbps)

        let ok = await Task.detached {
            DeskhubAgent.start(sources: picked, fps: fpsNum, bitrateMbps: bitrateNum)
        }.value

        isStarting = false
        isSharing = ok
        if ok {
            addresses = DeskhubAgent.localAddresses()
            startPolling()
        } else {
            startError = hasScreenRecording
                ? "Could not start sharing. The display may be disconnected, or the port is in use."
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
        // Thread Recv có thể tự dừng (lỗi socket) — UI phải theo, không thì người
        // dùng ngồi nhìn một màn hình phiên đã chết.
        if !DeskhubAgent.isRunning {
            isSharing = false
            stopPolling()
        }
    }
}
