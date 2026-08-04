import SwiftUI
import UIKit

struct TouchInputView: UIViewRepresentable {
    let model: StreamModel
    let videoRect: CGRect
    var blockedRect: CGRect = .zero
    var panMode = false
    var zoomed = false
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
        uiView.zoomed = zoomed
        uiView.onTransform = onTransform
    }
}

final class TouchCaptureUIView: UIView {
    weak var model: StreamModel?

    var videoRect: CGRect = .zero {
        didSet {
            guard videoRect != oldValue else { return }
            cursor = clampToVisible(cursor)
            layoutCursor()
        }
    }

    var blockedRect: CGRect = .zero

    var panMode = false

    var zoomed = false

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

    private var cursor = TrackpadCursor()
    private var lastDragLocation: CGPoint = .zero
    private var scrollCarry = 0.0

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

    private func layoutCursor() {
        guard let origin = cursor.screenPoint(in: videoRect) else { return }
        cursorView.frame.origin = origin
    }

    private func clampToVisible(_ point: TrackpadCursor) -> TrackpadCursor {
        point.clamped(to: videoRect, viewport: bounds.size)
    }

    override func layoutSubviews() {
        super.layoutSubviews()
        cursor = clampToVisible(cursor)
        layoutCursor()
    }

    private func moveCursor(by delta: CGPoint) {
        cursor = cursor.moved(by: delta, in: videoRect, viewport: bounds.size)
        layoutCursor()
        sendMove()
    }

    private func sendMove() {
        guard let point = cursor.normalized(in: videoRect) else { return }
        model?.mouseMove(nx: point.nx, ny: point.ny)
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
                transform(1, .zero, CGSize(width: delta.x, height: delta.y))
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
            transform(factor, gesture.location(in: self), .zero)
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
            if zoomed {
                transform(1, .zero, CGSize(width: translation.x, height: translation.y))
            } else {
                scrollRemote(by: translation.y)
            }
        default:
            break
        }
    }

    private func transform(_ factor: CGFloat, _ centroid: CGPoint, _ panDelta: CGSize) {
        scrollCarry = 0
        onTransform(factor, centroid, panDelta)
    }

    private func scrollRemote(by dragPoints: CGFloat) {
        let notches = dh_take_scroll_notches(Double(dragPoints), &scrollCarry)
        if notches != 0 { model?.mouseWheelNotches(notches) }
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
