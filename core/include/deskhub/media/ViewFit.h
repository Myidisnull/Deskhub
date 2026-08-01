#pragma once
#include "deskhub/input/PointerMap.h"

#include <cstdint>

namespace deskhub {

inline constexpr int32_t kNormalizedMax = kAbsCoordMax;
inline constexpr double kMaxViewZoom = 5.0;
inline constexpr double kZoomedThreshold = 1.01;
inline constexpr int32_t kViewerMarginPx = 48;

constexpr bool IsZoomed(double zoom) {
    return zoom > kZoomedThreshold;
}

struct ViewRect {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;

    constexpr double midX() const {
        return x + width / 2;
    }
    constexpr double midY() const {
        return y + height / 2;
    }
};

struct ViewTransform {
    double zoom = 1.0;
    double panX = 0;
    double panY = 0;
};

struct ViewSize {
    uint32_t width = 0;
    uint32_t height = 0;

    friend bool operator==(const ViewSize& a, const ViewSize& b) {
        return a.width == b.width && a.height == b.height;
    }
};

ViewSize ScaleToFit(uint32_t width, uint32_t height, uint32_t maxWidth, uint32_t maxHeight);

ViewRect FitVideoRect(double viewportW, double viewportH, double aspect,
    const ViewTransform& t = {});

ViewTransform ApplyGesture(const ViewTransform& cur, double factor, double centroidX,
    double centroidY, double panDeltaX, double panDeltaY, double viewportW, double viewportH,
    double aspect);

int32_t NormalizeAxis(double offset, double extent);

bool NormalizePointer(double px, double py, const ViewRect& rect, int32_t& nx, int32_t& ny);

}
