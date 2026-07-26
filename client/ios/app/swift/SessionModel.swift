// =============================================================================
// SessionModel.swift — trạng thái phiên cho SwiftUI, đối ứng ViewModel của Android.
//
// Quản lý toàn bộ luồng người dùng: kết nối → chọn nguồn → xem. View chỉ đọc thuộc
// tính và gọi action trên model, không chạm facade trực tiếp.
//
// LỖI LƯU BẰNG KHOÁ, KHÔNG PHẢI BẰNG CÂU ĐÃ DỊCH
//   `connectErrorKey` giữ khoá của bảng chữ chứ không phải chuỗi tiếng Anh/tiếng Việt
//   đã dựng sẵn. Ngôn ngữ đổi được ngay giữa chừng (nút EN/VI ở thanh trên cùng), nên
//   một câu đã dịch nằm trong model sẽ đứng nguyên bằng thứ tiếng cũ trong khi cả màn
//   hình quanh nó đã đổi.
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
    /// Khoá trong Strings, rỗng = không có lỗi. View gọi tr() lên nó.
    var connectErrorKey = ""
    var phase: Phase = .idle
    var statusLine = ""
    var endReason = ""
    var videoWidth: UInt32 = 0
    var videoHeight: UInt32 = 0

    // "Chỉ xem" — ô tick ở màn Kết nối. Chặn Ở ĐÂY chứ không ở view: input đi ra từ
    // ba chỗ khác nhau (trackpad, bàn phím ảo, thanh phím tắt), và một cái quên kiểm
    // tra là cả lựa chọn này thành vô nghĩa mà không ai biết. Chặn ở cửa duy nhất
    // xuống C++ thì không có đường nào lọt.
    var viewOnly: Bool = UserDefaults.standard.bool(forKey: "viewOnly") {
        didSet { UserDefaults.standard.set(viewOnly, forKey: "viewOnly") }
    }

    // Dãy RTT cho biểu đồ ở HUD màn xem, bóc từ dòng số liệu (xem parseRtt).
    var rttTrace: [Double] = []

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
        connectErrorKey = ""
        UserDefaults.standard.set(addr, forKey: "lastAddress")

        Task {
            // listSources CHẶN ~3 giây — Task.detached đẩy nó ra khỏi main actor, nếu
            // không thì giao diện đứng hình đúng lúc nó cần quay vòng chờ.
            let sources = await Task.detached { DeskhubClient.listSources(address: addr) }.value
            isConnecting = false
            Recents.remember(address: addr)
            if sources.count > 1 {
                screen = .sourcePicker(sources)
            } else {
                startStream(sourceId: sources.first?.id ?? 0)
            }
        }
    }

    // Bắt đầu xem.
    func startStream(sourceId: UInt8) {
        endReason = ""
        statusLine = ""
        rttTrace = []
        connectErrorKey = ""
        phase = .connecting
        // GĐ10: chìa mật khẩu + token đã lưu cho host này. Cả hai rỗng ở lần đầu —
        // khi đó host đòi mật khẩu sẽ đẩy phiên sang .needPassword và StreamView hiện
        // ô nhập.
        let saved = Credentials.forAddress(address)

        // Ô "Unlock with Face ID before connecting". Chỉ hỏi khi THẬT SỰ có bí mật đã
        // lưu để dùng: bắt quét mặt rồi mới hiện ô nhập mật khẩu là hai lớp xác thực
        // chồng lên nhau mà lớp đầu không bảo vệ gì cả.
        if saved != nil, Credentials.biometricEnabled, Credentials.biometricAvailable {
            Task { @MainActor in
                guard await Credentials.unlock(reason: tr("biometricUnlock")) else {
                    phase = .idle
                    screen = .connect
                    return
                }
                finishStart(sourceId: sourceId, credential: saved)
            }
            return
        }
        finishStart(sourceId: sourceId, credential: saved)
    }

    // Nửa sau của startStream, tách ra vì cửa Face ID ở trên là bất đồng bộ.
    private func finishStart(sourceId: UInt8, credential: HostCredential?) {
        // dh_start chỉ trả false khi chuỗi địa chỉ không phân tích được — lỗi của
        // người gõ, và nó phải được nói ra Ở MÀN KẾT NỐI chứ không phải bằng một màn
        // xem đen thui không giải thích gì.
        guard DeskhubClient.start(
            address: address,
            sourceId: sourceId,
            credential: credential
        ) else {
            phase = .idle
            connectErrorKey = "invalidAddress"
            screen = .connect
            return
        }
        screen = .stream
        startPolling()
    }

    // MARK: — Xác thực (GĐ10)

    /// Người dùng vừa nhập mật khẩu ở ô trên màn xem. `remember` = ô "Save the
    /// password for this machine".
    func submitPassword(_ password: String, remember: Bool) {
        guard !password.isEmpty else { return }
        if remember { Credentials.savePassword(password, for: address) }
        DeskhubClient.submitPassword(password)
        // Về .connecting ngay để ô nhập biến mất trong cùng một khung hình; nhịp poll
        // kế tiếp sẽ nói sự thật (streaming, hoặc ended nếu sai mật khẩu).
        phase = .connecting
    }

    /// Bí mật đã lưu cho từng host — mục "Saved passwords" ở màn kết nối.
    var savedCredentials: [HostCredential] { Credentials.all }

    func forgetCredential(_ address: String) {
        Credentials.forget(address)
    }

    func forgetAllCredentials() {
        Credentials.forgetAll()
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
    // Mọi hàm đi qua cùng một cửa `viewOnly`; tầng C++ tự bỏ qua khi chưa STREAMING. ---

    // Gõ một phím tắt rời (Esc/Tab/Enter/mũi tên... — thanh phím tắt của StreamView).
    func keyTap(vk: Int32, scan: Int32) {
        guard !viewOnly else { return }
        DeskhubClient.keyTap(vk: vk, scan: scan)
    }

    // Tổ hợp kiểu Ctrl+C từ thanh phím tắt.
    func keyChord(modVk: Int32, modScan: Int32, vk: Int32, scan: Int32) {
        guard !viewOnly else { return }
        DeskhubClient.keyChord(modVk: modVk, modScan: modScan, vk: vk, scan: scan)
    }

    func mouseMove(nx: Int32, ny: Int32) {
        guard !viewOnly else { return }
        DeskhubClient.mouseMove(nx: nx, ny: ny)
    }

    func mouseButton(_ button: MouseButton, down: Bool) {
        guard !viewOnly else { return }
        DeskhubClient.mouseButton(button, down: down)
    }

    func charTap(_ codepoint: UInt32) {
        guard !viewOnly else { return }
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
        if let rtt = Self.parseRtt(statusLine) {
            rttTrace.append(rtt)
            if rttTrace.count > 60 { rttTrace.removeFirst(rttTrace.count - 60) }
        }

        // GĐ10: token nhớ thiết bị chỉ về ĐÚNG MỘT LẦN, ngay sau khi đáp đúng mật
        // khẩu. Vét mỗi nhịp poll và cất ngay — bỏ lỡ là lần sau phải gõ lại.
        if let token = DeskhubClient.takeDeviceToken() {
            Credentials.saveToken(token, for: address)
        }

        if phase == .ended {
            endReason = DeskhubClient.endReason()
            stopPolling()
        }
    }

    // Bóc "RTT 4 ms" ra khỏi dòng số liệu mà ClientLoop dựng sẵn, thay vì mở thêm một
    // hàm C thứ hai chỉ để trả về đúng con số đó. Dòng ấy được dựng ở MỘT chỗ
    // (ClientLoop.cpp) và bản macOS/Windows cũng bóc RTT ra khỏi cùng chuỗi đó — mấy
    // client đọc cùng một nguồn thì không có cách nào lệch nhau.
    private static func parseRtt(_ line: String) -> Double? {
        guard let range = line.range(of: "RTT ") else { return nil }
        let rest = line[range.upperBound...]
        let digits = rest.prefix { $0.isNumber || $0 == "." }
        return Double(digits)
    }
}
