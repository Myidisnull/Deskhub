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
        let r = dh_video_rect(
            Double(size.width), Double(size.height), Double(aspect),
            DHViewTransform(zoom: Double(zoom), panX: Double(pan.width), panY: Double(pan.height))
        )
        return CGRect(x: r.x, y: r.y, width: r.width, height: r.height)
    }

    mutating func apply(
        factor: CGFloat, centroid: CGPoint, panDelta: CGSize, viewport: CGSize, aspect: CGFloat
    ) {
        guard viewport.width > 0, viewport.height > 0 else { return }
        let t = dh_apply_gesture(
            DHViewTransform(zoom: Double(zoom), panX: Double(pan.width), panY: Double(pan.height)),
            Double(factor), Double(centroid.x), Double(centroid.y),
            Double(panDelta.width), Double(panDelta.height),
            Double(viewport.width), Double(viewport.height), Double(aspect)
        )
        zoom = CGFloat(t.zoom)
        pan = CGSize(width: CGFloat(t.panX), height: CGFloat(t.panY))
    }
}
