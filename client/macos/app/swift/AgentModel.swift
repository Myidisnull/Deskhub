// =============================================================================
// AgentModel.swift — trạng thái vai HOST (chia sẻ máy này) cho SwiftUI.
//
// Đối ứng của cặp MainMenuWindow::DoShare + SessionWindow bên Windows: giữ tuỳ
// chọn phiên, danh sách địa chỉ IP cho hộp Host mode, và ảnh chụp trạng thái các
// nguồn đang chia sẻ cho màn "Deskhub - sharing".
//
// VÌ SAO CÓ CẢ startError
//   dha_start thất bại vì nhiều lý do rất khác nhau (cổng bận, thiếu quyền, nguồn
//   không lên hình) và tất cả đều chỉ hiện ra trong log. Người dùng bấm Share rồi
//   không thấy gì xảy ra là trải nghiệm tệ nhất có thể — nên model giữ một dòng lý
//   do để MainMenuView hiện alert (Windows dùng MessageBox cùng chỗ).
// =============================================================================
import Foundation
import Observation

// Một dòng IP cho hộp Host mode ("tên card" + "ip"). Nút Copy chép IP TRẦN.
struct LocalAddress: Identifiable, Hashable {
    let ip: String
    let name: String
    var id: String { ip + name }
}

@MainActor @Observable
final class AgentModel {
    // Tuỳ chọn phiên, nhớ lại cho lần sau. KHÔNG có `port` và `allowInput`: cổng
    // luôn 47777 và chuột/bàn phím luôn được chia sẻ (chốt 2026-07-27).
    var fps: Int = UserDefaults.standard.object(forKey: "shareFps") as? Int ?? 60
    var bitrateMbps: Int = UserDefaults.standard.object(forKey: "shareBitrate") as? Int ?? 20
    // Trần cạnh dài của khung gửi đi; 0 = gửi đúng độ phân giải native. Màn Mac là
    // Retina nên native gần như luôn quá to cho một đường truyền thật — lý do đầy
    // đủ ở AgentOptions::maxDim (cpp/AgentLoop.h).
    var maxDim: Int = UserDefaults.standard.object(forKey: "shareMaxDim") as? Int ?? 1920

    // Địa chỉ IPv4 của máy này, hiện ngay ở màn chính (giống MainMenuWindow).
    var addresses: [LocalAddress] = []

    // Trạng thái phiên đang chạy.
    var isSharing = false
    var isStarting = false
    var startError = ""
    var rows: [AgentSourceStatus] = []

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

    // MARK: - Địa chỉ

    func loadAddresses() {
        addresses = DeskhubAgent.localAddresses()
    }

    // MARK: - Vòng đời phiên

    // Giống DoShare bên Windows: quét màn hình NGAY LÚC BẤM rồi chia sẻ HẾT — không
    // có bước chọn nguồn, danh sách chốt ở đây và không đổi trong suốt phiên (cắm
    // thêm màn hình giữa chừng thì phải dừng rồi chia sẻ lại).
    func startSharing() async -> Bool {
        guard !isStarting, !isSharing else { return false }
        isStarting = true
        startError = ""
        UserDefaults.standard.set(fps, forKey: "shareFps")
        UserDefaults.standard.set(bitrateMbps, forKey: "shareBitrate")
        UserDefaults.standard.set(maxDim, forKey: "shareMaxDim")

        let fpsNum = UInt32(max(1, fps))
        let bitrateNum = UInt32(max(1, bitrateMbps))
        // 0 đi thẳng xuống (= native); mọi giá trị khác kẹp dưới ở 640 để không ai
        // gõ tay ra một trần nhỏ hơn cả kMinEncode* rồi ngồi nhìn phiên treo.
        let maxDimNum = maxDim <= 0 ? UInt32(0) : UInt32(max(640, maxDim))

        // Quét (~2s) + start (~10s) đều chặn — dồn cả hai xuống nền.
        let ok = await Task.detached {
            let picked = DeskhubAgent.listShareSources()
            guard !picked.isEmpty else { return false }
            return DeskhubAgent.start(
                sources: picked,
                fps: fpsNum,
                bitrateMbps: bitrateNum,
                maxDim: maxDimNum
            )
        }.value

        isStarting = false
        isSharing = ok
        if ok {
            startPolling()
        } else {
            startError = hasScreenRecording
                ? "Could not start sharing. No display was found, the display may be "
                + "disconnected, or UDP port 47777 is already in use."
                : "Screen Recording permission is required. Grant it in System Settings, "
                + "then quit and reopen Deskhub."
        }
        return ok
    }

    func stopSharing() {
        stopPolling()
        DeskhubAgent.stop()
        isSharing = false
        rows = []
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
        rows = DeskhubAgent.status()
        // Thread Recv có thể tự dừng (lỗi socket, mọi màn hình bị rút) — UI phải
        // theo và dọn nốt phần còn lại (socket, capture), không thì người dùng
        // ngồi nhìn một màn hình phiên đã chết. Giống RunAgent bên Windows trả về.
        if !DeskhubAgent.isRunning {
            stopSharing()
        }
    }
}
