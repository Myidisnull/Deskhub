#include "deskhub/media/ViewFit.h"

#include <algorithm>

namespace deskhub {

namespace {

double Clamp(double v, double lo, double hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

}

ViewRect FitVideoRect(double viewportW, double viewportH, double aspect,
    const ViewTransform& t) {
    if (viewportW <= 0 || viewportH <= 0) return ViewRect{};

    double baseW = viewportW;
    double baseH = viewportH;
    if (aspect > 0) {
        baseH = viewportW / aspect;
        if (baseH > viewportH) {
            baseH = viewportH;
            baseW = viewportH * aspect;
        }
    }

    const double zoom = t.zoom > 0 ? t.zoom : 1.0;
    const double width = baseW * zoom;
    const double height = baseH * zoom;

    const double maxPanX = std::max(0.0, (width - viewportW) / 2);
    const double maxPanY = std::max(0.0, (height - viewportH) / 2);

    ViewRect r;
    r.width = width;
    r.height = height;
    r.x = (viewportW - width) / 2 + Clamp(t.panX, -maxPanX, maxPanX);
    r.y = (viewportH - height) / 2 + Clamp(t.panY, -maxPanY, maxPanY);
    return r;
}

ViewSize ScaleToFit(uint32_t width, uint32_t height, uint32_t maxWidth, uint32_t maxHeight) {
    if (!width || !height) return ViewSize{};
    if (!maxWidth || !maxHeight) return ViewSize{width, height};

    const double scale = std::min({1.0, double(maxWidth) / width, double(maxHeight) / height});
    return ViewSize{std::max(1u, uint32_t(width * scale)),
        std::max(1u, uint32_t(height * scale))};
}

ViewTransform ApplyGesture(const ViewTransform& cur, double factor, double centroidX,
    double centroidY, double panDeltaX, double panDeltaY, double viewportW, double viewportH,
    double aspect) {
    if (viewportW <= 0 || viewportH <= 0) return cur;

    const double zoom = cur.zoom > 0 ? cur.zoom : 1.0;
    const double newZoom = Clamp(zoom * factor, 1.0, kMaxViewZoom);
    const double ratio = newZoom / zoom;

    ViewTransform next{newZoom, cur.panX, cur.panY};
    if (ratio != 1.0) {
        next.panX = (centroidX - viewportW / 2) * (1 - ratio) + cur.panX * ratio;
        next.panY = (centroidY - viewportH / 2) * (1 - ratio) + cur.panY * ratio;
    }
    next.panX += panDeltaX;
    next.panY += panDeltaY;

    const ViewRect clamped = FitVideoRect(viewportW, viewportH, aspect, next);
    next.panX = clamped.midX() - viewportW / 2;
    next.panY = clamped.midY() - viewportH / 2;
    return next;
}

int32_t NormalizeAxis(double offset, double extent) {
    if (extent <= 1) return 0;
    const double clamped = Clamp(offset, 0.0, extent - 1);
    return int32_t(clamped * kNormalizedMax / (extent - 1));
}

bool NormalizePointer(double px, double py, const ViewRect& rect, int32_t& nx, int32_t& ny) {
    if (rect.width <= 0 || rect.height <= 0) return false;
    if (px < rect.x || py < rect.y) return false;
    if (px >= rect.x + rect.width || py >= rect.y + rect.height) return false;

    nx = NormalizeAxis(px - rect.x, rect.width);
    ny = NormalizeAxis(py - rect.y, rect.height);
    return true;
}

}
