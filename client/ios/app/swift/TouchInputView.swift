// =============================================================================
// TouchInputView.swift — trackpad ảo phủ lên vùng hiển thị, kiểu bàn di chuột laptop.
//                        Đối ứng TrackpadOverlay bên Android.
//
// VÌ SAO TRACKPAD CHỨ KHÔNG PHẢI CHẠM-TRỰC-TIẾP
//   Chạm thẳng vào điểm muốn click nghe hợp lý nhưng khó dùng thật: ngón tay che
//   mất chỗ cần bấm và không bấm chính xác được mục tiêu nhỏ. Trackpad tách ngón
//   tay khỏi con trỏ: con trỏ LUÔN hiện, ngón rê ở đâu cũng được — kể cả vùng đen
//   letterbox quanh video (overlay phủ CẢ vùng hiển thị, không chỉ khung video).
//
// CỬ CHỈ -> CHUỘT
//   Rê ngón       = di con trỏ.
//   Tap 1 lần     = click trái TẠI CON TRỎ (chờ hết cửa sổ double-tap mới nổ).
//   Tap 2 lần     = click phải tại con trỏ.
//   Giữ rồi kéo   = giữ chuột trái và rê (kéo cửa sổ, bôi đen), nhấc tay là nhả.
//   Chụm 2 ngón   = phóng 1×..5× kiểu xem ảnh: điểm nằm giữa hai ngón DÍNH ở đó.
//   Rê 2 ngón     = dời sang khu vực khác.
//   Hai cái sau chạy SONG SONG: tay vừa chụm vừa trượt thì khung vừa nở vừa đi theo.
//
// MỘT NGÓN CÓ HAI VAI, VÀ PHẢI CÓ CÔNG TẮC
//   Phóng lên rồi thì việc hay làm nhất là VUỐT MỘT NGÓN để ngắm chỗ khác — nhưng một
//   ngón đang là trackpad. Không có cách nào đoán được ý người dùng từ chính cú vuốt
//   đó, nên `panMode` quyết định: bật thì một ngón dời khung, tắt thì một ngón di
//   chuột. Công tắc là viên thuốc "Pan/Pointer" ở lớp điều khiển, chỉ hiện khi đang
//   phóng (lúc 1× không có gì để dời).
//   Lúc panMode bật, MỌI cử chỉ một ngón đều im — kể cả tap: đang vuốt để ngắm mà lỡ
//   click xuống host thì tệ hơn nhiều so với việc phải chạm công tắc một cái.
//   Các recognizer mặc định loại trừ lẫn nhau: pan một ngón có
//   maximumNumberOfTouches = 1 nên ngón thứ hai chạm xuống là nó kết thúc, nhường
//   cho pinch/pan-hai-ngón; hai cái sau chạy song song với nhau (xem delegate).
//
// KHUNG VIDEO LÀ MỘT RECT TÍNH SẴN, OVERLAY NÀY THÌ KHÔNG BỊ PHÓNG
//   `ViewTransform.frame(in:aspect:)` (ViewTransform.swift) tính KHUNG video hiển thị
//   (aspect-fit, phóng quanh tâm chụm, rồi dịch pan đã kẹp) và StreamView dựng lớp
//   video đúng bằng khung đó. Overlay này thì KHÔNG bị phóng — nó luôn phủ trọn vùng
//   nhìn.
//   Vì thế con trỏ được lưu ở toạ độ CHUẨN HOÁ 0..1 theo khung video chứ không
//   phải điểm ảnh màn hình: khung có phóng/kéo thế nào thì vị trí trên desktop của
//   host vẫn y nguyên, và toạ độ 0..65535 gửi đi chỉ là phép nhân.
//   Hệ quả dễ chịu: phóng càng to, một milimet ngón tay càng ít pixel host — trỏ
//   càng chính xác.
//
// KHUNG HÌNH CHỈ NHÚC NHÍCH KHI NGƯỜI DÙNG TỰ KÉO
//   Bản đầu làm ngược: con trỏ rê tới mép thì khung tự chạy theo. Nghe tiện, dùng thì
//   không: vừa chụm phóng vào một góc, chạm ngón một cái là khung giật đi chỗ khác
//   (con trỏ đang đứng đâu đó ngoài vùng nhìn) — coi như mất luôn khu vực vừa chọn.
//   Giờ đảo lại: phóng và kéo là việc của HAI NGÓN, và chỉ hai ngón. Zoom đã chọn thì
//   nằm im cho tới khi người dùng đổi.
//   Đổi lại, con trỏ bị kẹp trong PHẦN ĐANG NHÌN THẤY của khung (giao của khung video
//   với màn hình) thay vì cả khung: rê một ngón không bao giờ đẩy được nó ra ngoài
//   rìa để rồi mất dấu, còn muốn với tới vùng khác thì kéo hai ngón sang đó.
//
// VÙNG MÙ CHO BẢNG ĐIỀU KHIỂN
//   Overlay này phủ trọn màn hình, kể cả phần nằm dưới bảng điều khiển của StreamView.
//   `blockedRect` (toạ độ cửa sổ) là khung bảng đang chiếm: mọi điểm rơi vào đó bị
//   point(inside:) trả false, nên UIKit không hit-test vào view này và các gesture
//   recognizer ở đây không hề thấy cú chạm — bấm nút không làm con trỏ nhảy.
//
// LIÊN QUAN: ViewTransform.swift (toán zoom/pan), StreamView.swift (nơi đặt overlay),
//            SessionModel (chuyển tiếp xuống C++)
// =============================================================================
import SwiftUI
import UIKit

struct TouchInputView: UIViewRepresentable {
    let model: SessionModel
    /// Khung video đang hiển thị (toạ độ overlay) — do StreamView tính bằng
    /// `ViewTransform.frame(in:aspect:)`. Con trỏ kẹp trong khung này và chuẩn hoá
    /// theo nó.
    let videoRect: CGRect
    /// Khung của lớp điều khiển, toạ độ .global — xem `blockedRect` bên dưới.
    var blockedRect: CGRect = .zero
    /// true = một ngón DỜI KHUNG thay vì di chuột (xem đầu file).
    var panMode = false
    /// Cử chỉ hai ngón đổi khung nhìn: (hệ số zoom TĂNG THÊM, tâm chụm theo toạ độ
    /// overlay, đoạn kéo tính bằng điểm màn hình). Đây là đường DUY NHẤT làm khung hình
    /// dịch chuyển — xem đầu file.
    var onTransform: (CGFloat, CGPoint, CGSize) -> Void = { _, _, _ in }

    func makeUIView(context _: Context) -> TouchCaptureUIView {
        let view = TouchCaptureUIView()
        view.model = model
        return view
    }

    func updateUIView(_ uiView: TouchCaptureUIView, context _: Context) {
        uiView.model = model
        uiView.videoRect = videoRect
        uiView.blockedRect = blockedRect
        uiView.panMode = panMode
        uiView.onTransform = onTransform
    }
}

final class TouchCaptureUIView: UIView {
    weak var model: SessionModel?

    /// Khung video hiển thị — con trỏ sống trong đây. Khung đổi (phóng, kéo, xoay máy)
    /// thì vị trí trên desktop của con trỏ KHÔNG đổi (nó lưu chuẩn hoá), chỉ kẹp lại
    /// cho nằm trong phần đang nhìn thấy rồi vẽ lại.
    var videoRect: CGRect = .zero {
        didSet {
            guard videoRect != oldValue else { return }
            cursor = clampToVisible(cursor)
            layoutCursor()
        }
    }

    // Vùng không nhận chạm (toạ độ CỬA SỔ, do StreamView đo bằng .global) — chỗ bảng
    // điều khiển đang đứng. .zero = không khoét gì.
    var blockedRect: CGRect = .zero

    /// Một ngón dời khung thay vì di chuột. Lúc bật, MỌI cử chỉ một ngón đều im — kể
    /// cả tap và giữ-rồi-kéo: đang vuốt để ngắm mà lỡ tay click xuống host thì tệ hơn
    /// nhiều so với việc phải chạm công tắc một cái.
    var panMode = false

    /// Báo lên StreamView khi khung nhìn phải đổi — xem `TouchInputView.onTransform`.
    var onTransform: (CGFloat, CGPoint, CGSize) -> Void = { _, _, _ in }

    // Mũi tên con trỏ: SF Symbol trắng + bóng đen để nổi trên mọi nền video.
    private let cursorView: UIImageView = {
        let view = UIImageView(image: UIImage(systemName: "cursorarrow"))
        view.tintColor = .white
        view.layer.shadowColor = UIColor.black.cgColor
        view.layer.shadowOpacity = 0.9
        view.layer.shadowOffset = .zero
        view.layer.shadowRadius = 1.5
        view.frame = CGRect(x: 0, y: 0, width: 18, height: 20)
        return view
    }()

    /// Vị trí con trỏ, CHUẨN HOÁ 0..1 theo khung video — xem đầu file.
    private var cursor = CGPoint(x: 0.5, y: 0.5)
    private var lastDragLocation: CGPoint = .zero

    /// Đúng hai recognizer hai-ngón — xem phần delegate ở cuối file.
    private var zoomGestures: [UIGestureRecognizer] = []

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .clear
        // Bật đa chạm cho pinch/pan-hai-ngón. Các recognizer một ngón vẫn an toàn:
        // pan cursor giới hạn 1 chạm, còn tap mặc định đòi đúng 1 ngón.
        isMultipleTouchEnabled = true
        addSubview(cursorView)

        let doubleTap = UITapGestureRecognizer(target: self, action: #selector(handleDoubleTap))
        doubleTap.numberOfTapsRequired = 2
        let singleTap = UITapGestureRecognizer(target: self, action: #selector(handleSingleTap))
        // Tap 1 phải chờ chắc chắn không phải tap 2 — giá của việc phân biệt hai cử chỉ.
        singleTap.require(toFail: doubleTap)
        let pan = UIPanGestureRecognizer(target: self, action: #selector(handlePan(_:)))
        pan.maximumNumberOfTouches = 1
        let longPress = UILongPressGestureRecognizer(
            target: self, action: #selector(handleLongPress(_:))
        )
        let pinch = UIPinchGestureRecognizer(target: self, action: #selector(handlePinch(_:)))
        pinch.delegate = self
        let twoFingerPan = UIPanGestureRecognizer(
            target: self, action: #selector(handleTwoFingerPan(_:))
        )
        twoFingerPan.minimumNumberOfTouches = 2
        twoFingerPan.maximumNumberOfTouches = 2
        twoFingerPan.delegate = self
        zoomGestures = [pinch, twoFingerPan]

        addGestureRecognizer(doubleTap)
        addGestureRecognizer(singleTap)
        addGestureRecognizer(pan)
        addGestureRecognizer(longPress)
        addGestureRecognizer(pinch)
        addGestureRecognizer(twoFingerPan)
    }

    @available(*, unavailable)
    required init?(coder _: NSCoder) { fatalError("init(coder:) is not supported") }

    // Chặn ngay từ HIT-TEST, không phải ở tầng gesture: view nào không "chứa" điểm
    // chạm thì UIKit không giao touch cho nó, nên các recognizer ở đây không thấy gì
    // cả. Quy về toạ độ cửa sổ vì blockedRect do SwiftUI đo bằng .global.
    override func point(inside point: CGPoint, with event: UIEvent?) -> Bool {
        guard super.point(inside: point, with: event) else { return false }
        guard !blockedRect.isEmpty else { return true }
        return !blockedRect.contains(convert(point, to: nil))
    }

    // MARK: - Con trỏ

    /// Vị trí con trỏ trên màn hình (toạ độ overlay), suy từ vị trí chuẩn hoá.
    private var cursorPoint: CGPoint {
        CGPoint(
            x: videoRect.minX + cursor.x * videoRect.width,
            y: videoRect.minY + cursor.y * videoRect.height
        )
    }

    // Đỉnh mũi tên của "cursorarrow" nằm ở góc trên-trái icon -> origin đặt đúng
    // vị trí con trỏ.
    private func layoutCursor() {
        cursorView.frame.origin = cursorPoint
    }

    /// Kẹp vị trí (chuẩn hoá) vào PHẦN KHUNG ĐANG NHÌN THẤY — giao của khung video với
    /// màn hình. Lúc 1× khung nằm gọn trong màn hình nên đây đúng là kẹp về 0..1 như
    /// cũ; phóng to thì nó chặn con trỏ lẻn ra ngoài rìa màn hình rồi mất dấu.
    private func clampToVisible(_ point: CGPoint) -> CGPoint {
        guard videoRect.width > 0, videoRect.height > 0 else { return point }
        let visible = videoRect.intersection(bounds)
        guard !visible.isNull, visible.width > 0, visible.height > 0 else { return point }
        return CGPoint(
            x: min(
                max((visible.minX - videoRect.minX) / videoRect.width, point.x),
                (visible.maxX - videoRect.minX) / videoRect.width
            ),
            y: min(
                max((visible.minY - videoRect.minY) / videoRect.height, point.y),
                (visible.maxY - videoRect.minY) / videoRect.height
            )
        )
    }

    // Bounds đổi (xoay máy) cũng phải kẹp lại: phần nhìn thấy được vừa khác đi.
    override func layoutSubviews() {
        super.layoutSubviews()
        cursor = clampToVisible(cursor)
        layoutCursor()
    }

    private func moveCursor(by delta: CGPoint) {
        guard videoRect.width > 0, videoRect.height > 0 else { return }
        cursor = clampToVisible(CGPoint(
            x: cursor.x + delta.x / videoRect.width,
            y: cursor.y + delta.y / videoRect.height
        ))
        layoutCursor()
        sendMove()
    }

    // Vị trí con trỏ đã chuẩn hoá sẵn — chỉ việc trải ra thang 0..65535 mà
    // InputInjector bên host mong đợi.
    private func sendMove() {
        guard videoRect.width > 0, videoRect.height > 0 else { return }
        model?.mouseMove(
            nx: Int32((cursor.x * 65535).rounded()),
            ny: Int32((cursor.y * 65535).rounded())
        )
    }

    // Host cũng có người dùng thật di chuột được — gửi lại vị trí con trỏ ngay
    // trước mỗi cú click để click rơi đúng chỗ con trỏ đang hiển thị.
    private func click(_ button: MouseButton) {
        sendMove()
        model?.mouseButton(button, down: true)
        model?.mouseButton(button, down: false)
    }

    // MARK: - Cử chỉ

    @objc private func handleSingleTap() {
        guard !panMode else { return }
        click(.left)
    }

    @objc private func handleDoubleTap() {
        guard !panMode else { return }
        click(.right)
    }

    // Một ngón: dời khung hay di con trỏ, tuỳ `panMode`. Cùng một delta, khác chỗ đổ.
    @objc private func handlePan(_ gesture: UIPanGestureRecognizer) {
        let location = gesture.location(in: self)
        switch gesture.state {
        case .began:
            lastDragLocation = location
        case .changed:
            let delta = CGPoint(
                x: location.x - lastDragLocation.x,
                y: location.y - lastDragLocation.y
            )
            lastDragLocation = location
            if panMode {
                onTransform(1, .zero, CGSize(width: delta.x, height: delta.y))
            } else {
                moveCursor(by: delta)
            }
        default:
            break
        }
    }

    @objc private func handleLongPress(_ gesture: UILongPressGestureRecognizer) {
        guard !panMode else { return }
        let location = gesture.location(in: self)
        switch gesture.state {
        case .began:
            lastDragLocation = location
            sendMove()
            model?.mouseButton(.left, down: true)
        case .changed:
            moveCursor(by: CGPoint(
                x: location.x - lastDragLocation.x,
                y: location.y - lastDragLocation.y
            ))
            lastDragLocation = location
        case .ended, .cancelled, .failed:
            model?.mouseButton(.left, down: false)
        default:
            break
        }
    }

    // Báo lên phần TĂNG THÊM rồi tự trừ về 1: StreamView giữ mức zoom tuyệt đối và
    // kẹp nó, nên ở đây không cần biết đang phóng bao nhiêu. Tâm chụm đi kèm để phóng
    // neo đúng chỗ hai ngón đang đặt.
    @objc private func handlePinch(_ gesture: UIPinchGestureRecognizer) {
        switch gesture.state {
        case .began:
            gesture.scale = 1
        case .changed:
            let factor = gesture.scale
            gesture.scale = 1
            onTransform(factor, gesture.location(in: self), .zero)
        default:
            break
        }
    }

    // Chạy SONG SONG với pinch (xem delegate) chứ không loại trừ: tay vừa chụm vừa
    // trượt thì khung phải vừa nở vừa đi theo, đúng như xem ảnh.
    @objc private func handleTwoFingerPan(_ gesture: UIPanGestureRecognizer) {
        switch gesture.state {
        case .began:
            gesture.setTranslation(.zero, in: self)
        case .changed:
            let translation = gesture.translation(in: self)
            gesture.setTranslation(.zero, in: self)
            // Tâm bỏ qua: không đổi zoom thì công thức neo không dùng tới nó.
            onTransform(1, .zero, CGSize(width: translation.x, height: translation.y))
        default:
            break
        }
    }
}

// Chụm và rê hai ngón gần như luôn xảy ra CÙNG LÚC — không cho chạy song song thì
// UIKit chỉ nhận một cái và cử chỉ kia bị nuốt.
//
// Chỉ ĐÚNG cặp đó thôi, không phải trả true cho mọi cặp: các cử chỉ một ngón phải
// giữ nguyên luật loại trừ mặc định, kẻo long-press (đang giữ chuột trái) sống sót
// qua cả lần phóng và host nhận một cú kéo không ai muốn.
extension TouchCaptureUIView: UIGestureRecognizerDelegate {
    func gestureRecognizer(
        _ gesture: UIGestureRecognizer,
        shouldRecognizeSimultaneouslyWith other: UIGestureRecognizer
    ) -> Bool {
        zoomGestures.contains(gesture) && zoomGestures.contains(other)
    }
}
