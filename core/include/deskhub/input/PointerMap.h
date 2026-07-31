#pragma once
#include <cstdint>

namespace deskhub {

inline constexpr int32_t kAbsCoordMax = 65535;
inline constexpr int32_t kWheelDeltaPerNotch = 120;

constexpr int32_t ClampAbsCoord(int32_t normalized) {
    if (normalized < 0) return 0;
    if (normalized > kAbsCoordMax) return kAbsCoordMax;
    return normalized;
}

constexpr double AbsCoordToAxis(int32_t normalized, double origin, double extent) {
    return origin + double(ClampAbsCoord(normalized)) / double(kAbsCoordMax) * extent;
}

constexpr int64_t AbsCoordToPixel(int32_t normalized, int64_t origin, int64_t extent) {
    if (extent <= 0) return origin;
    return origin + int64_t(ClampAbsCoord(normalized)) * extent / kAbsCoordMax;
}

constexpr int32_t AxisToAbsCoord(int64_t pixel, int64_t origin, int64_t extent) {
    if (extent <= 0) return 0;
    const int64_t offset = pixel - origin;
    if (offset <= 0) return 0;
    const int64_t scaled = offset * kAbsCoordMax / extent;
    return scaled >= kAbsCoordMax ? kAbsCoordMax : int32_t(scaled);
}

constexpr int32_t WheelNotches(int32_t delta) {
    if (const int32_t whole = delta / kWheelDeltaPerNotch) return whole;
    if (delta > 0) return 1;
    if (delta < 0) return -1;
    return 0;
}

}
