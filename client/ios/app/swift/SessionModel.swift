// =============================================================================
// SessionModel.swift — trạng thái phiên cho SwiftUI, đối ứng ViewModel của Android.
//
// Quản lý toàn bộ luồng người dùng: kết nối → chọn nguồn → xem. View chỉ đọc thuộc
// tính và gọi action trên model, không chạm facade trực tiếp.
//
// KHÔNG CÒN "CHỈ XEM" (bỏ 2026-07-27)
//   Mọi phiên đều gửi chuột/bàn phím. Các hàm chuyển tiếp input bên dưới vì thế không
//   có cửa kiểm tra nào; chúng vẫn tồn tại để view chỉ nói chuyện với model, không gọi
//   thẳng facade.
//
// NHIỀU MÀN HÌNH: `sources` được GIỮ LẠI sau khi chọn
//   Host chia sẻ tất cả màn hình, nên đổi màn hình giữa phiên là việc thường. Giữ
//   danh sách ở đây thì màn xem đổi được ngay mà không phải hỏi lại host (mất 3 giây).
// =============================================================================
import Foundation
import Observation

enum AppScreen: Sendable {
    case connect
    case sourcePicker([Source])
    case stream
}

@MainActor @Observable
final class SessionModel {
    var screen: AppScreen = .connect
    var address: String = UserDefaults.standard.string(forKey: "lastAddress") ?? ""
    var isConnecting = false
    /// Câu lỗi hiện ở màn kết nối, rỗng = không có lỗi.
    var connectError = ""
    var phase: Phase = .idle
    var statusLine = ""
    var endReason = ""
    var videoWidth: UInt32 = 0
    var videoHeight: UInt32 = 0

    /// Mọi nguồn host đang chia sẻ + nguồn đang xem, để đổi màn hình giữa phiên.
    var sources: [Source] = []
    var currentSourceId: UInt8 = 0

    private var pollTimer: Timer?

    // MARK: - Vòng đời phiên

    // Hỏi host xem nó chia sẻ những gì rồi đi tiếp: nhiều nguồn thì cho chọn, không
    // thì vào thẳng. Danh sách rỗng gộp hai trường hợp — host im lặng (bản trước GĐ6
    // không biết LIST_SOURCES / mất gói) và host không chia sẻ gì — thành một: cứ vào
    // NGUỒN 0 và để ClientSession báo lỗi thật. Một nguồn thì bỏ qua luôn màn chọn:
    // bắt người dùng bấm một cái không có lựa chọn nào là một bước thừa.
    func connect() {
        let addr = address.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !addr.isEmpty, !isConnecting else { return }
        address = addr
        isConnecting = true
        connectError = ""
        UserDefaults.standard.set(addr, forKey: "lastAddress")

        Task {
            // listSources CHẶN ~3 giây — Task.detached đẩy nó ra khỏi main actor, nếu
            // không thì giao diện đứng hình đúng lúc nó cần quay vòng chờ.
            let found = await Task.detached { DeskhubClient.listSources(address: addr) }.value
            isConnecting = false
            sources = found
            if found.count > 1 {
                screen = .sourcePicker(found)
            } else {
                startStream(sourceId: found.first?.id ?? 0)
            }
        }
    }

    // Bắt đầu xem. dh_start chỉ trả false khi chuỗi địa chỉ không phân tích được —
    // lỗi của người gõ, và nó phải được nói ra Ở MÀN KẾT NỐI chứ không phải bằng một
    // màn xem đen thui không giải thích gì.
    func startStream(sourceId: UInt8) {
        endReason = ""
        statusLine = ""
        connectError = ""
        phase = .connecting
        currentSourceId = sourceId
        guard DeskhubClient.start(address: address, sourceId: sourceId) else {
            phase = .idle
            connectError = "Could not connect to \(address). Enter just the IP address."
            screen = .connect
            return
        }
        screen = .stream
        startPolling()
    }

    /// Đổi sang màn hình khác của CÙNG host, không rời màn xem.
    ///
    /// Giao thức không có lệnh "đổi nguồn" và không cần có: mỗi cặp (client, nguồn)
    /// vốn là một phiên riêng, nên đổi = đóng phiên cũ rồi mở phiên mới với sourceId
    /// khác. Lớp video (dh_set_layer) do StreamView giữ, không phụ thuộc vòng đời
    /// phiên, nên không phải dựng lại gì.
    func switchSource(to sourceId: UInt8) {
        guard sourceId != currentSourceId else { return }
        stopPolling()
        DeskhubClient.stop()
        endReason = ""
        statusLine = ""
        videoWidth = 0
        videoHeight = 0
        phase = .connecting
        currentSourceId = sourceId
        guard DeskhubClient.start(address: address, sourceId: sourceId) else {
            phase = .idle
            endReason = "Could not switch display."
            return
        }
        startPolling()
    }

    // Dừng phiên và quay về màn hình kết nối.
    func disconnect() {
        stopPolling()
        DeskhubClient.stop()
        phase = .idle
        statusLine = ""
        screen = .connect
    }

    // --- Điều khiển từ touch/bàn phím ảo (StreamView + TouchInputView/KeyInputView).
    // Không có cửa kiểm tra nào; tầng C++ tự bỏ qua khi chưa STREAMING. ---

    // Gõ một phím tắt rời (Esc/Tab/Enter/mũi tên... — thanh phím tắt của StreamView).
    func keyTap(vk: Int32, scan: Int32) {
        DeskhubClient.keyTap(vk: vk, scan: scan)
    }

    // Tổ hợp kiểu Ctrl+C từ thanh phím tắt.
    func keyChord(modVk: Int32, modScan: Int32, vk: Int32, scan: Int32) {
        DeskhubClient.keyChord(modVk: modVk, modScan: modScan, vk: vk, scan: scan)
    }

    func mouseMove(nx: Int32, ny: Int32) {
        DeskhubClient.mouseMove(nx: nx, ny: ny)
    }

    func mouseButton(_ button: MouseButton, down: Bool) {
        DeskhubClient.mouseButton(button, down: down)
    }

    func charTap(_ codepoint: UInt32) {
        DeskhubClient.charTap(codepoint)
    }

    // UI cần gọi khi StreamView xuất hiện/biến mất.
    func streamViewAppeared() {
        startPolling()
    }

    func streamViewDisappeared() {
        stopPolling()
    }

    // Hỏi C++ mỗi 500ms để cập nhật overlay.
    private func startPolling() {
        stopPolling()
        let timer = Timer(timeInterval: 0.5, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.poll() }
        }
        // .common chứ không phải mode mặc định: khi người dùng đang kéo (scroll thanh
        // phím tắt) run loop ở tracking mode và timer mode mặc định đứng im — phase
        // đổi sẽ không được vét cho tới khi họ buông tay.
        RunLoop.main.add(timer, forMode: .common)
        pollTimer = timer
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
            stopPolling()
        }
    }
}
