#include "deskhub/input/TrackpadCursor.h"

namespace deskhub {

namespace {

double Clamp(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

bool VisibleRect(const ViewRect& video, double viewportW, double viewportH, ViewRect& out) {
    if (video.width <= 0 || video.height <= 0) return false;
    if (viewportW <= 0 || viewportH <= 0) return false;

    const double left = video.x > 0 ? video.x : 0;
    const double top = video.y > 0 ? video.y : 0;
    const double right = video.x + video.width < viewportW ? video.x + video.width : viewportW;
    const double bottom = video.y + video.height < viewportH ? video.y + video.height : viewportH;
    if (right <= left || bottom <= top) return false;

    out = {left, top, right - left, bottom - top};
    return true;
}

}

TrackpadCursor ClampToVisible(TrackpadCursor cur, const ViewRect& video, double viewportW,
    double viewportH) {
    ViewRect visible;
    if (!VisibleRect(video, viewportW, viewportH, visible)) return cur;

    return {Clamp(cur.x, (visible.x - video.x) / video.width,
                (visible.x + visible.width - video.x) / video.width),
        Clamp(cur.y, (visible.y - video.y) / video.height,
            (visible.y + visible.height - video.y) / video.height)};
}

TrackpadCursor MoveCursorBy(TrackpadCursor cur, double dx, double dy, const ViewRect& video,
    double viewportW, double viewportH) {
    if (video.width <= 0 || video.height <= 0) return cur;
    return ClampToVisible({cur.x + dx / video.width, cur.y + dy / video.height}, video, viewportW,
        viewportH);
}

bool CursorScreenPoint(TrackpadCursor cur, const ViewRect& video, double& px, double& py) {
    if (video.width <= 0 || video.height <= 0) return false;
    px = video.x + cur.x * video.width;
    py = video.y + cur.y * video.height;
    return true;
}

bool NormalizeCursor(TrackpadCursor cur, const ViewRect& video, int32_t& nx, int32_t& ny) {
    if (video.width <= 0 || video.height <= 0) return false;
    nx = NormalizeAxis(Clamp(cur.x, 0.0, 1.0) * video.width, video.width);
    ny = NormalizeAxis(Clamp(cur.y, 0.0, 1.0) * video.height, video.height);
    return true;
}

}
