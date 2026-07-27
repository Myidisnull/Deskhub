// =============================================================================
// SessionModel.swift — hai model của vai CLIENT, chia đúng theo hai cửa sổ Windows:
//
//   SessionModel — hộp Client mode ở màn chính (MainMenuWindow::DoConnect): ô địa
//                  chỉ + hỏi host đang chia sẻ gì.
//   StreamModel  — MỘT cửa sổ xem (ViewerFrame trong Viewer.cpp): sở hữu một
//                  ClientSession riêng, nhiều cửa sổ là nhiều model song song.
//
// View chỉ đọc thuộc tính và gọi action trên model, không chạm facade trực tiếp.
// =============================================================================
import AVFoundation
import Foundation
import Observation

@MainActor @Observable
final class SessionModel {
    var address: String = UserDefaults.standard.string(forKey: "lastAddress") ?? ""
    var isConnecting = false
    var connectError = ""

    // Trả về danh sách nguồn; caller (MainMenuView) quyết định đi tiếp màn nào —
    // danh sách đi theo Route.sourcePicker, model không giữ lại.
    func listSources() async -> [Source] {
        guard !address.isEmpty else { return [] }
        isConnecting = true
        connectError = ""
        let addr = address
        UserDefaults.standard.set(addr, forKey: "lastAddress")

        let found = await Task.detached { DeskhubClient.listSources(address: addr) }.value
        isConnecting = false
        return found
    }
}

// MARK: - Một cửa sổ xem

@MainActor @Observable
final class StreamModel {
    let address: String
    let sourceId: UInt8
    let sourceName: String

    var phase: Phase = .connecting
    var statusLine = ""
    var endReason = ""
    var videoWidth: UInt32 = 0
    var videoHeight: UInt32 = 0
    // Không mở được phiên (địa chỉ sai / thiếu GPU) — cửa sổ báo rồi tự đóng, giống
    // nhánh MessageBox "Could not open a viewing session" của RunViewer.
    var failedToStart = false

    // Khoá chuột (chế độ tương đối) — bật/tắt bằng F9, đối ứng client Windows.
    var mouseLocked = false

    // KHÔNG có `viewOnly` và KHÔNG có `hostAcceptsInput` (bỏ 2026-07-27): mọi phiên
    // đều gửi chuột/bàn phím, và host thì luôn nhận.

    private var session: ClientSession?
    // Layer do RemoteView giao, có thể tới TRƯỚC khi phiên mở xong (start chạy nền)
    // — giữ lại để áp ngay khi phiên sẵn sàng.
    private var layer: AVSampleBufferDisplayLayer?
    private var pollTimer: Timer?

    init(address: String, sourceId: UInt8, sourceName: String) {
        self.address = address
        self.sourceId = sourceId
        self.sourceName = sourceName
    }

    // MARK: - Vòng đời

    // CHẶN ~1s trong Task nền. Gọi đúng một lần khi cửa sổ hiện ra.
    func start() async {
        let addr = address
        let sid = sourceId
        let opened = await Task.detached { ClientSession.start(address: addr, sourceId: sid) }.value
        guard let opened else {
            failedToStart = true
            phase = .ended
            return
        }
        session = opened
        opened.setLayer(layer)
        startPolling()
    }

    func disconnect() {
        stopPolling()
        mouseLocked = false
        session?.stop()
        session = nil
        phase = .idle
        statusLine = ""
    }

    // MARK: - Layer video (RemoteView giao/thu hồi)

    func setLayer(_ newLayer: AVSampleBufferDisplayLayer?) {
        layer = newLayer
        session?.setLayer(newLayer)
    }

    // MARK: - Chuyển tiếp input (RemoteView gọi)

    func key(vk: Int32, scan: Int32, down: Bool) {
        session?.key(vk: vk, scan: scan, down: down)
    }

    func releaseAllInput() {
        session?.releaseAllInput()
    }

    func mouseMove(nx: Int32, ny: Int32) {
        session?.mouseMove(nx: nx, ny: ny)
    }

    func mouseMoveRel(dx: Int32, dy: Int32) {
        session?.mouseMoveRel(dx: dx, dy: dy)
    }

    func mouseButton(_ button: MouseButton, down: Bool) {
        session?.mouseButton(button, down: down)
    }

    func mouseWheel(_ delta: Int32) {
        session?.mouseWheel(delta)
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
        guard let session else { return }
        phase = session.phase()
        statusLine = session.statusLine()
        videoWidth = session.videoWidth()
        videoHeight = session.videoHeight()

        if phase == .ended {
            endReason = session.endReason()
            mouseLocked = false
            stopPolling()
        }
    }
}
