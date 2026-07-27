// =============================================================================
// SessionModel.swift — trạng thái phiên XEM cho SwiftUI.
//                      Đối ứng client/ios/app/swift/SessionModel.swift.
//
// Quản lý luồng người dùng của vai client: kết nối → chọn nguồn → xem. View chỉ đọc
// thuộc tính và gọi action trên model, không chạm facade trực tiếp.
//
// =============================================================================
import AppKit
import Foundation
import Observation

@MainActor @Observable
final class SessionModel {
    var address: String = UserDefaults.standard.string(forKey: "lastAddress") ?? ""
    var isConnecting = false
    var connectError = ""
    var phase: Phase = .idle
    var statusLine = ""
    var endReason = ""
    var videoWidth: UInt32 = 0
    var videoHeight: UInt32 = 0

    // Khoá chuột (chế độ tương đối) — bật/tắt bằng F9, đối ứng client Windows.
    var mouseLocked = false

    // KHÔNG có `viewOnly` và KHÔNG có `hostAcceptsInput` (bỏ 2026-07-27): mọi phiên
    // đều gửi chuột/bàn phím, và host thì luôn nhận. Cả hai từng là hai đường dẫn tới
    // cùng một trạng thái "gõ không ăn" mà giao diện phải giải thích.

    /// Mọi nguồn host đang chia sẻ + nguồn đang xem, để đổi màn hình giữa phiên.
    /// Host chia sẻ TẤT CẢ màn hình nên đây là việc thường; giữ danh sách ở đây thì
    /// màn xem đổi được ngay mà không phải hỏi lại host (mất 3 giây).
    var sources: [Source] = []
    var currentSourceId: UInt8 = 0

    private var pollTimer: Timer?

    // MARK: - Vòng đời phiên

    // Trả về danh sách nguồn; caller (AppModel) quyết định đi tiếp màn nào.
    func listSources() async -> [Source] {
        guard !address.isEmpty else { return [] }
        isConnecting = true
        connectError = ""
        let addr = address
        UserDefaults.standard.set(addr, forKey: "lastAddress")

        let found = await Task.detached { DeskhubClient.listSources(address: addr) }.value
        isConnecting = false
        sources = found
        return found
    }

    func startStream(sourceId: UInt8) {
        endReason = ""
        statusLine = ""
        phase = .connecting
        mouseLocked = false
        currentSourceId = sourceId
        DeskhubClient.start(address: address, sourceId: sourceId)
        startPolling()
    }

    /// Đổi sang màn hình khác của CÙNG host, không rời màn xem.
    ///
    /// Giao thức không có lệnh "đổi nguồn" và không cần có: mỗi cặp (client, nguồn)
    /// vốn là một phiên riêng, nên đổi = đóng phiên cũ rồi mở phiên mới với sourceId
    /// khác. Lớp video do RemoteView giữ, không phụ thuộc vòng đời phiên.
    func switchSource(to sourceId: UInt8) {
        guard sourceId != currentSourceId else { return }
        stopPolling()
        DeskhubClient.stop()
        endReason = ""
        statusLine = ""
        videoWidth = 0
        videoHeight = 0
        mouseLocked = false
        phase = .connecting
        currentSourceId = sourceId
        DeskhubClient.start(address: address, sourceId: sourceId)
        startPolling()
    }

    func disconnect() {
        stopPolling()
        mouseLocked = false
        DeskhubClient.stop()
        phase = .idle
        statusLine = ""
    }

    // MARK: - Chuyển tiếp input (StreamView/RemoteView gọi)

    // Không còn cửa kiểm tra nào ở đây: bản trước có `inputBlocked` (viewOnly ||
    // !hostAcceptsInput), cả hai đã bỏ 2026-07-27 nên mọi hàm chỉ chuyển tiếp thẳng.

    func key(vk: Int32, scan: Int32, down: Bool) {
        DeskhubClient.key(vk: vk, scan: scan, down: down)
    }

    func releaseAllInput() {
        DeskhubClient.releaseAllInput()
    }

    func mouseMove(nx: Int32, ny: Int32) {
        DeskhubClient.mouseMove(nx: nx, ny: ny)
    }

    func mouseMoveRel(dx: Int32, dy: Int32) {
        DeskhubClient.mouseMoveRel(dx: dx, dy: dy)
    }

    func mouseButton(_ button: MouseButton, down: Bool) {
        DeskhubClient.mouseButton(button, down: down)
    }

    func mouseWheel(_ delta: Int32) {
        DeskhubClient.mouseWheel(delta)
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
        phase = DeskhubClient.phase()
        statusLine = DeskhubClient.statusLine()
        videoWidth = DeskhubClient.videoWidth()
        videoHeight = DeskhubClient.videoHeight()

        if phase == .ended {
            endReason = DeskhubClient.endReason()
            mouseLocked = false
            stopPolling()
        }
    }
}
