import SwiftUI
import UIKit

struct TouchInputView: UIViewRepresentable {
    let model: SessionModel
    let videoRect: CGRect
    var blockedRect: CGRect = .zero
    var panMode = false
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

    var videoRect: CGRect = .zero {
        didSet {
            guard videoRect != oldValue else { return }
            cursor = clampToVisible(cursor)
            layoutCursor()
        }
    }

    var blockedRect: CGRect = .zero

    var panMode = false

    var onTransform: (CGFloat, CGPoint, CGSize) -> Void = { _, _, _ in }

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

    private var cursor = CGPoint(x: 0.5, y: 0.5)
    private var lastDragLocation: CGPoint = .zero

    private var zoomGestures: [UIGestureRecognizer] = []

    override init(frame: CGRect) {
        super.init(frame: frame)
        backgroundColor = .clear
        isMultipleTouchEnabled = true
        addSubview(cursorView)

        let doubleTap = UITapGestureRecognizer(target: self, action: #selector(handleDoubleTap))
        doubleTap.numberOfTapsRequired = 2
        let singleTap = UITapGestureRecognizer(target: self, action: #selector(handleSingleTap))
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

    override func point(inside point: CGPoint, with event: UIEvent?) -> Bool {
        guard super.point(inside: point, with: event) else { return false }
        guard !blockedRect.isEmpty else { return true }
        return !blockedRect.contains(convert(point, to: nil))
    }

    private var cursorPoint: CGPoint {
        CGPoint(
            x: videoRect.minX + cursor.x * videoRect.width,
            y: videoRect.minY + cursor.y * videoRect.height
        )
    }

    private func layoutCursor() {
        cursorView.frame.origin = cursorPoint
    }

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

    private func sendMove() {
        guard videoRect.width > 0, videoRect.height > 0 else { return }
        model?.mouseMove(
            nx: Int32((cursor.x * 65535).rounded()),
            ny: Int32((cursor.y * 65535).rounded())
        )
    }

    private func click(_ button: MouseButton) {
        sendMove()
        model?.mouseButton(button, down: true)
        model?.mouseButton(button, down: false)
    }

    @objc private func handleSingleTap() {
        guard !panMode else { return }
        click(.left)
    }

    @objc private func handleDoubleTap() {
        guard !panMode else { return }
        click(.right)
    }

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

    @objc private func handleTwoFingerPan(_ gesture: UIPanGestureRecognizer) {
        switch gesture.state {
        case .began:
            gesture.setTranslation(.zero, in: self)
        case .changed:
            let translation = gesture.translation(in: self)
            gesture.setTranslation(.zero, in: self)
            onTransform(1, .zero, CGSize(width: translation.x, height: translation.y))
        default:
            break
        }
    }
}

extension TouchCaptureUIView: UIGestureRecognizerDelegate {
    func gestureRecognizer(
        _ gesture: UIGestureRecognizer,
        shouldRecognizeSimultaneouslyWith other: UIGestureRecognizer
    ) -> Bool {
        zoomGestures.contains(gesture) && zoomGestures.contains(other)
    }
}
