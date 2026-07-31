import SwiftUI

struct ViewTransform {
    static let maxZoom: CGFloat = 5

    var zoom: CGFloat = 1
    var pan: CGSize = .zero

    var isZoomed: Bool { zoom > 1.01 }

    static func baseFrame(in size: CGSize, aspect: CGFloat) -> CGRect {
        ViewTransform().frame(in: size, aspect: aspect)
    }

    func frame(in size: CGSize, aspect: CGFloat) -> CGRect {
        guard size.width > 0, size.height > 0 else { return .zero }
        var baseWidth = size.width
        var baseHeight = size.height
        if aspect > 0 {
            baseHeight = size.width / aspect
            if baseHeight > size.height {
                baseHeight = size.height
                baseWidth = size.height * aspect
            }
        }
        let width = baseWidth * zoom
        let height = baseHeight * zoom
        let maxX = max(0, (width - size.width) / 2)
        let maxY = max(0, (height - size.height) / 2)
        return CGRect(
            x: (size.width - width) / 2 + min(max(-maxX, pan.width), maxX),
            y: (size.height - height) / 2 + min(max(-maxY, pan.height), maxY),
            width: width, height: height
        )
    }

    mutating func apply(
        factor: CGFloat, centroid: CGPoint, panDelta: CGSize, viewport: CGSize, aspect: CGFloat
    ) {
        guard viewport.width > 0, viewport.height > 0 else { return }
        let newZoom = min(max(1, zoom * factor), Self.maxZoom)
        let ratio = newZoom / zoom
        var next = pan
        if ratio != 1 {
            next = CGSize(
                width: (centroid.x - viewport.width / 2) * (1 - ratio) + pan.width * ratio,
                height: (centroid.y - viewport.height / 2) * (1 - ratio) + pan.height * ratio
            )
        }
        next.width += panDelta.width
        next.height += panDelta.height
        zoom = newZoom
        pan = next
        let clamped = frame(in: viewport, aspect: aspect)
        pan = CGSize(
            width: clamped.midX - viewport.width / 2,
            height: clamped.midY - viewport.height / 2
        )
    }
}
