import CoreGraphics

struct TrackpadCursor: Equatable {
    private var position = CGPoint(x: 0.5, y: 0.5)

    private var raw: DHCursor { DHCursor(x: Double(position.x), y: Double(position.y)) }

    private init(_ cursor: DHCursor) {
        position = CGPoint(x: cursor.x, y: cursor.y)
    }

    init() {}

    func clamped(to video: CGRect, viewport: CGSize) -> TrackpadCursor {
        TrackpadCursor(
            dh_cursor_clamp(raw, video.dhRect, Double(viewport.width), Double(viewport.height))
        )
    }

    func moved(by delta: CGPoint, in video: CGRect, viewport: CGSize) -> TrackpadCursor {
        TrackpadCursor(
            dh_cursor_move(
                raw, Double(delta.x), Double(delta.y), video.dhRect,
                Double(viewport.width), Double(viewport.height)
            )
        )
    }

    func screenPoint(in video: CGRect) -> CGPoint? {
        var px = 0.0
        var py = 0.0
        guard dh_cursor_point(raw, video.dhRect, &px, &py) else { return nil }
        return CGPoint(x: px, y: py)
    }

    func normalized(in video: CGRect) -> (nx: Int32, ny: Int32)? {
        var nx: Int32 = 0
        var ny: Int32 = 0
        guard dh_cursor_normalize(raw, video.dhRect, &nx, &ny) else { return nil }
        return (nx, ny)
    }
}

extension CGRect {
    var dhRect: DHViewRect {
        DHViewRect(
            x: Double(origin.x), y: Double(origin.y),
            width: Double(size.width), height: Double(size.height)
        )
    }
}
