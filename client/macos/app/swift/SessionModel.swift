// =============================================================================
// SessionModel.swift — trạng thái phiên XEM cho SwiftUI.
//                      Đối ứng client/ios/app/swift/SessionModel.swift.
//
// Quản lý luồng người dùng của vai client: kết nối → chọn nguồn → xem. View chỉ đọc
// thuộc tính và gọi action trên model, không chạm facade trực tiếp.
//
// CLIPBOARD ĐI QUA ĐÂY, KHÔNG QUA VIEW
//   NSPasteboard phải chạm trên main thread, và vòng hỏi 500ms sẵn có là chỗ rẻ nhất
//   để làm việc đó — khỏi thêm một timer thứ hai. Chiều gửi đi cũng vậy: ta so
//   changeCount mỗi nhịp poll thay vì bắt sự kiện (macOS không có sự kiện clipboard —
//   xem agent/ClipboardSync.h).
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

    // GĐ9: host có nhận điều khiển không (cờ inputAccepted trong HELLO_ACK, poll qua
    // facade). false = phiên chỉ-xem từ phía host — khác `viewOnly` là lựa chọn của
    // người dùng máy này; UI phải NÓI ra để "gõ không ăn" không giống lỗi mạng.
    private(set) var hostAcceptsInput = true

    // "Chỉ xem" — ô tick ở màn Kết nối của bản thiết kế. Chặn Ở ĐÂY chứ không ở view:
    // input đi ra từ bốn chỗ khác nhau trong RemoteView (phím, phím bổ trợ, chuột,
    // con lăn), và một cái quên kiểm tra là cả lựa chọn này thành vô nghĩa mà không ai
    // biết. Chặn ở cửa duy nhất xuống C++ thì không có đường nào lọt.
    var viewOnly: Bool = UserDefaults.standard.bool(forKey: "viewOnly") {
        didSet {
            UserDefaults.standard.set(viewOnly, forKey: "viewOnly")
            // Bật giữa phiên trong lúc đang giữ phím = kẹt phím ở máy kia.
            if viewOnly { DeskhubClient.releaseAllInput() }
        }
    }

    // Dãy RTT cho biểu đồ ở HUD màn xem, bóc từ dòng số liệu (xem parseRtt).
    var rttTrace: [Double] = []

    private var pollTimer: Timer?
    private var lastPasteboardChange = NSPasteboard.general.changeCount

    // MARK: - Vòng đời phiên

    // Trả về danh sách nguồn; caller (AppModel) quyết định đi tiếp màn nào.
    func listSources() async -> [Source] {
        guard !address.isEmpty else { return [] }
        isConnecting = true
        connectError = ""
        let addr = address
        UserDefaults.standard.set(addr, forKey: "lastAddress")

        let sources = await Task.detached { DeskhubClient.listSources(address: addr) }.value
        isConnecting = false
        return sources
    }

    func startStream(sourceId: UInt8) {
        endReason = ""
        statusLine = ""
        rttTrace = []
        phase = .connecting
        mouseLocked = false
        hostAcceptsInput = true // chỉ biết thật sau HELLO_ACK — poll cập nhật
        // Chụp mốc clipboard NGAY khi vào phiên: không thì thứ đang nằm sẵn trong
        // clipboard bị coi là "vừa copy" và bắn sang host ngay lập tức.
        lastPasteboardChange = NSPasteboard.general.changeCount
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

    //
    // Mọi hàm ở đây đi qua cùng một cửa `inputBlocked`. releaseAllInput là NGOẠI LỆ
    // có chủ ý: nó chỉ nhả thứ đang bị giữ, nên chặn nó lại mới là chuyện gây kẹt phím.

    // Hai đường tới "chỉ xem": người dùng tự chọn (viewOnly), hoặc host không nhận
    // điều khiển (GĐ9 — hostAcceptsInput). Cửa duy nhất, không có đường nào lọt.
    private var inputBlocked: Bool { viewOnly || !hostAcceptsInput }

    func key(vk: Int32, scan: Int32, down: Bool) {
        guard !inputBlocked else { return }
        DeskhubClient.key(vk: vk, scan: scan, down: down)
    }

    func releaseAllInput() {
        DeskhubClient.releaseAllInput()
    }

    func mouseMove(nx: Int32, ny: Int32) {
        guard !inputBlocked else { return }
        DeskhubClient.mouseMove(nx: nx, ny: ny)
    }

    func mouseMoveRel(dx: Int32, dy: Int32) {
        guard !inputBlocked else { return }
        DeskhubClient.mouseMoveRel(dx: dx, dy: dy)
    }

    func mouseButton(_ button: MouseButton, down: Bool) {
        guard !inputBlocked else { return }
        DeskhubClient.mouseButton(button, down: down)
    }

    func mouseWheel(_ delta: Int32) {
        guard !inputBlocked else { return }
        DeskhubClient.mouseWheel(delta)
    }

    // MARK: - Hỏi vòng trạng thái + clipboard

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
        let accepts = DeskhubClient.inputAccepted()
        if accepts != hostAcceptsInput {
            hostAcceptsInput = accepts
            if !accepts { mouseLocked = false } // không có gì để khoá ở phiên chỉ-xem
        }
        if let rtt = Self.parseRtt(statusLine) {
            rttTrace.append(rtt)
            if rttTrace.count > 60 { rttTrace.removeFirst(rttTrace.count - 60) }
        }
        syncClipboard()

        if phase == .ended {
            endReason = DeskhubClient.endReason()
            mouseLocked = false
            stopPolling()
        }
    }

    // Hai chiều trong một hàm, và thứ tự QUAN TRỌNG: nhận trước, gửi sau. Đặt văn
    // bản của host vào clipboard làm changeCount tăng, nên nếu gửi trước thì nhánh
    // gửi ở nhịp SAU sẽ bắn ngược đúng thứ vừa nhận — vòng lặp vô tận. Ghi nhận
    // changeCount ngay sau khi tự ghi là thứ cắt vòng đó.
    private func syncClipboard() {
        let pb = NSPasteboard.general

        let incoming = DeskhubClient.takeRemoteClipboard()
        if !incoming.isEmpty {
            pb.clearContents()
            pb.setString(incoming, forType: .string)
            lastPasteboardChange = pb.changeCount
            return
        }

        guard pb.changeCount != lastPasteboardChange else { return }
        lastPasteboardChange = pb.changeCount
        if let text = pb.string(forType: .string), !text.isEmpty {
            DeskhubClient.setClipboard(text)
        }
    }

    // Bóc "RTT 4 ms" ra khỏi dòng số liệu mà ClientLoop dựng sẵn, thay vì mở thêm một
    // hàm C thứ hai chỉ để trả về đúng con số đó. Dòng ấy được dựng ở MỘT chỗ
    // (ClientLoop.cpp) và bản Windows cũng bóc RTT ra khỏi cùng chuỗi đó — hai client
    // đọc cùng một nguồn thì không có cách nào lệch nhau.
    private static func parseRtt(_ line: String) -> Double? {
        guard let range = line.range(of: "RTT ") else { return nil }
        let rest = line[range.upperBound...]
        let digits = rest.prefix { $0.isNumber || $0 == "." }
        return Double(digits)
    }
}
