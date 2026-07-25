// =============================================================================
// RemoteView.swift — NSView vừa HIỂN THỊ video vừa BẮT chuột/phím.
//                    Đối ứng client/ios/app/swift/VideoLayerView.swift +
//                    TouchInputView.swift + KeyInputView.swift gộp làm một, và đối
//                    ứng client/windows/ui/ViewerWindow + input/InputCapture.
//
// VÌ SAO GỘP LÀM MỘT VIEW (khác iOS tách ba)
//   Trên iOS, video là một UIView còn cú chạm và bàn phím ảo là hai lớp phủ riêng —
//   bắt buộc, vì UIKit không cho một view vừa nhận touch vừa làm first responder cho
//   bàn phím ảo một cách gọn gàng. macOS thì ngược lại: một NSView là first responder
//   nhận trọn keyDown/mouseMoved/scrollWheel, và chính nó cũng là view chứa layer
//   video. Tách ra chỉ tạo thêm ranh giới phải đồng bộ toạ độ.
//
// HAI CHẾ ĐỘ CHUỘT (đối ứng F9 của client Windows)
//   TUYỆT ĐỐI (mặc định) — con trỏ chuột của macOS chạy tự do; ta gửi toạ độ CHUẨN
//     HOÁ 0..65535 trong khung video. Dùng cho làm việc: IDE, trình duyệt, tài liệu.
//   TƯƠNG ĐỐI (F9)       — con trỏ bị khoá và ẩn; ta gửi DELTA thô. Bắt buộc cho
//     game FPS: game đọc delta và tự áp sensitivity, còn toạ độ tuyệt đối thì nhân
//     vật xoay giật cục và không bao giờ quay quá mép màn hình.
//     CGAssociateMouseAndMouseCursorPosition(false) là thứ tách con trỏ khỏi chuột
//     vật lý; thiếu nó thì con trỏ vẫn chạy ra khỏi cửa sổ và ta mất event.
//
// ⚠ isFlipped = true
//   NSView mặc định gốc toạ độ DƯỚI-trái; giao thức và khung video dùng gốc TRÊN-
//   trái. Lật ở đây một lần thay vì trừ ngược ở mỗi chỗ tính toán — quên một chỗ là
//   chuột đi ngược chiều dọc, lỗi rất dễ lọt.
//
// LIÊN QUAN: swift/StreamView.swift (nơi nhúng), swift/DeskhubClient.swift,
//            cpp/input/MacKeyMap.h (bảng phím, phía C++ giữ bản duy nhất)
// =============================================================================
import AppKit
import AVFoundation
import SwiftUI

// MARK: - NSView

final class RemoteVideoView: NSView {
    // Model để đẩy input xuống. weak vì SessionModel sống theo AppModel, dài hơn view.
    weak var model: SessionModel?

    // Kích thước video đàm phán được — quyết định khung nào trong view là video thật
    // (phần còn lại là viền đen letterbox).
    var videoSize: CGSize = .zero {
        didSet { needsLayout = true }
    }

    // Chế độ khoá chuột. StreamView đọc lại qua onLockChanged để cập nhật nhãn nút.
    private(set) var mouseLocked = false
    var onLockChanged: ((Bool) -> Void)?

    private var trackingArea: NSTrackingArea?
    // Cờ modifier lần trước, để flagsChanged suy ra được phím nào vừa nhấn/nhả.
    private var lastFlags: NSEvent.ModifierFlags = []

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        wantsLayer = true
        layer?.backgroundColor = NSColor.black.cgColor
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    // Layer video là backing layer của chính view này: frame giải mã xong đi thẳng từ
    // VideoToolbox lên compositor, không qua CPU và không qua SwiftUI.
    override func makeBackingLayer() -> CALayer {
        let videoLayer = AVSampleBufferDisplayLayer()
        videoLayer.videoGravity = .resizeAspect
        videoLayer.backgroundColor = NSColor.black.cgColor
        return videoLayer
    }

    var displayLayer: AVSampleBufferDisplayLayer? {
        layer as? AVSampleBufferDisplayLayer
    }

    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }

    // Cú click đầu tiên vào một cửa sổ chưa active bình thường chỉ để KÍCH HOẠT cửa
    // sổ và không tới view. Ở đây phải tới: người dùng bấm vào khung video là muốn
    // bấm vào máy kia, không phải muốn bấm vào Deskhub.
    override func acceptsFirstMouse(for _: NSEvent?) -> Bool { true }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let trackingArea { removeTrackingArea(trackingArea) }
        // .activeInKeyWindow + .inVisibleRect: chỉ theo dõi khi cửa sổ đang key và
        // chỉ trong phần đang thấy — không đốt CPU khi app ở nền.
        let area = NSTrackingArea(
            rect: .zero,
            options: [.mouseMoved, .mouseEnteredAndExited, .activeInKeyWindow, .inVisibleRect],
            owner: self,
            userInfo: nil
        )
        addTrackingArea(area)
        trackingArea = area
    }

    // MARK: - Khung video thật bên trong view

    // Layer dùng .resizeAspect nên video nằm giữa và có viền đen hai bên (hoặc trên
    // dưới). Toạ độ chuẩn hoá phải tính theo KHUNG VIDEO chứ không theo cả view —
    // tính nhầm thì con trỏ ở máy kia lệch đúng bằng bề rộng viền.
    private var videoRect: CGRect {
        guard videoSize.width > 0, videoSize.height > 0 else { return bounds }
        let viewAspect = bounds.width / max(bounds.height, 1)
        let videoAspect = videoSize.width / videoSize.height
        if videoAspect > viewAspect {
            let fitHeight = bounds.width / videoAspect
            return CGRect(x: 0, y: (bounds.height - fitHeight) / 2,
                          width: bounds.width, height: fitHeight)
        }
        let fitWidth = bounds.height * videoAspect
        return CGRect(x: (bounds.width - fitWidth) / 2, y: 0,
                      width: fitWidth, height: bounds.height)
    }

    // Điểm trong view -> toạ độ chuẩn hoá 0..65535 trong khung video, hoặc nil nếu
    // con trỏ đang nằm trên viền đen (không có pixel nào tương ứng ở máy kia).
    private func normalize(_ point: NSPoint) -> (nx: Int32, ny: Int32)? {
        let rect = videoRect
        guard rect.width > 1, rect.height > 1 else { return nil }
        guard rect.contains(point) else { return nil }
        // Chia cho (kích thước - 1) để mép phải/dưới đạt ĐÚNG 65535 — khớp hàm
        // Normalize của client Windows, và host chia ngược lại bằng đúng mẫu số đó.
        let nx = (point.x - rect.minX) / (rect.width - 1) * 65535
        let ny = (point.y - rect.minY) / (rect.height - 1) * 65535
        return (Int32(max(0, min(65535, nx))), Int32(max(0, min(65535, ny))))
    }

    // MARK: - Khoá chuột (chế độ tương đối)

    func setMouseLocked(_ on: Bool) {
        guard mouseLocked != on else { return }
        mouseLocked = on
        if on {
            // Tách con trỏ khỏi chuột vật lý: chuột vẫn sinh delta nhưng con trỏ đứng
            // yên, nên nó không bao giờ lạc ra khỏi cửa sổ hay sang màn hình khác.
            CGAssociateMouseAndMouseCursorPosition(0)
            NSCursor.hide()
        } else {
            CGAssociateMouseAndMouseCursorPosition(1)
            NSCursor.unhide()
        }
        onLockChanged?(on)
    }

    // MARK: - Bàn phím

    override func keyDown(with event: NSEvent) {
        // F9 (keyCode 0x65) là phím THOÁT HIỂM — xử lý tại chỗ, không gửi đi. Không có
        // nó thì người dùng khoá chuột xong không còn cách nào nhả ra bằng bàn phím.
        if event.keyCode == 0x65 {
            setMouseLocked(!mouseLocked)
            return
        }
        // isARepeat: macOS tự lặp phím khi giữ. Bỏ qua bản lặp — host đã nhận cú nhấn
        // và tự lặp theo cấu hình CỦA NÓ; gửi thêm chỉ tốn gói và làm game thấy phím
        // bị nhấn lại liên tục.
        guard !event.isARepeat else { return }
        sendKey(event.keyCode, down: true)
    }

    override func keyUp(with event: NSEvent) {
        guard event.keyCode != 0x65 else { return }
        sendKey(event.keyCode, down: false)
    }

    // Phím bổ trợ không sinh keyDown/keyUp mà chỉ sinh flagsChanged — ta phải tự suy
    // ra nhấn hay nhả bằng cách so cờ mới với cờ cũ.
    override func flagsChanged(with event: NSEvent) {
        let flags = event.modifierFlags
        let mask = maskFor(keyCode: event.keyCode)
        if mask.isEmpty {
            lastFlags = flags
            return
        }
        let nowDown = flags.contains(mask)
        let wasDown = lastFlags.contains(mask)
        lastFlags = flags
        guard nowDown != wasDown else { return }
        sendKey(event.keyCode, down: nowDown)
    }

    private func maskFor(keyCode: UInt16) -> NSEvent.ModifierFlags {
        switch keyCode {
        case 0x38, 0x3C: .shift
        case 0x3B, 0x3E: .control
        case 0x3A, 0x3D: .option
        case 0x36, 0x37: .command
        case 0x39: .capsLock
        default: []
        }
    }

    private func sendKey(_ macKeyCode: UInt16, down: Bool) {
        guard let mapped = DeskhubClient.mapKey(macKeyCode) else { return }
        model?.key(vk: mapped.vk, scan: mapped.scan, down: down)
    }

    // MARK: - Chuột

    override func mouseMoved(with event: NSEvent) { handleMove(event) }
    override func mouseDragged(with event: NSEvent) { handleMove(event) }
    override func rightMouseDragged(with event: NSEvent) { handleMove(event) }
    override func otherMouseDragged(with event: NSEvent) { handleMove(event) }

    private func handleMove(_ event: NSEvent) {
        guard let model else { return }
        if mouseLocked {
            // deltaX/deltaY là delta THÔ của thiết bị, không qua tăng tốc con trỏ —
            // đúng thứ game FPS cần (docs/07 §5).
            let dx = Int32(event.deltaX.rounded())
            let dy = Int32(event.deltaY.rounded())
            if dx != 0 || dy != 0 { model.mouseMoveRel(dx: dx, dy: dy) }
            return
        }
        let point = convert(event.locationInWindow, from: nil)
        if let norm = normalize(point) { model.mouseMove(nx: norm.nx, ny: norm.ny) }
    }

    override func mouseDown(with _: NSEvent) { button(.left, down: true) }
    override func mouseUp(with _: NSEvent) { button(.left, down: false) }
    override func rightMouseDown(with _: NSEvent) { button(.right, down: true) }
    override func rightMouseUp(with _: NSEvent) { button(.right, down: false) }
    override func otherMouseDown(with _: NSEvent) { button(.middle, down: true) }
    override func otherMouseUp(with _: NSEvent) { button(.middle, down: false) }

    private func button(_ btn: MouseButton, down: Bool) {
        // Bấm vào khung video cũng là cách người dùng lấy lại focus bàn phím sau khi
        // họ bấm ra thanh nút bên ngoài.
        if down { window?.makeFirstResponder(self) }
        model?.mouseButton(btn, down: down)
    }

    override func scrollWheel(with event: NSEvent) {
        // Giao thức mang delta theo bội của 120 (WHEEL_DELTA của Windows). Trackpad
        // sinh rất nhiều event nhỏ có scrollingDeltaY < 1 — làm tròn lên ít nhất một
        // nấc theo dấu, không thì cuộn bằng trackpad hoàn toàn không có tác dụng.
        let raw = event.hasPreciseScrollingDeltas ? event.scrollingDeltaY / 10 : event.scrollingDeltaY
        guard raw != 0 else { return }
        let notches = max(1, Int32(abs(raw).rounded()))
        model?.mouseWheel(raw > 0 ? notches * 120 : -notches * 120)
    }

    // Mất focus giữa lúc đang giữ phím = kẹt phím ở máy kia. Nhả hết và bỏ khoá chuột
    // ngay — người dùng Cmd-Tab đi mà con trỏ vẫn bị khoá là tình huống rất khó thoát.
    override func resignFirstResponder() -> Bool {
        setMouseLocked(false)
        model?.releaseAllInput()
        return true
    }
}

// MARK: - Cầu nối SwiftUI

struct RemoteView: NSViewRepresentable {
    let model: SessionModel
    let videoSize: CGSize
    let onLayerReady: (AVSampleBufferDisplayLayer?) -> Void
    let onLockChanged: (Bool) -> Void

    func makeNSView(context _: Context) -> RemoteVideoView {
        let view = RemoteVideoView()
        view.model = model
        view.videoSize = videoSize
        view.onLockChanged = onLockChanged
        onLayerReady(view.displayLayer)
        // Giao bàn phím cho view ngay khi nó xuất hiện, khỏi bắt người dùng bấm một
        // cái vô nghĩa vào khung hình trước khi gõ được.
        DispatchQueue.main.async { view.window?.makeFirstResponder(view) }
        return view
    }

    func updateNSView(_ nsView: RemoteVideoView, context _: Context) {
        nsView.videoSize = videoSize
    }

    // Layer phải được thu hồi TRƯỚC khi view bị hủy — thread Decode còn enqueue vào
    // một layer đã chết là lỗi vòng đời (xem ClientLoop.h, cơ chế bắt tay layer).
    static func dismantleNSView(_ nsView: RemoteVideoView, coordinator _: ()) {
        nsView.setMouseLocked(false)
        DeskhubClient.setLayer(nil)
        nsView.model = nil
    }
}
