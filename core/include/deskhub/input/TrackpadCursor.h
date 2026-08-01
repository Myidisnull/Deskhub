#pragma once
#include "deskhub/media/ViewFit.h"

#include <cstdint>

namespace deskhub {

struct TrackpadCursor {
    double x = 0.5;
    double y = 0.5;

    friend bool operator==(const TrackpadCursor& a, const TrackpadCursor& b) {
        return a.x == b.x && a.y == b.y;
    }
};

TrackpadCursor ClampToVisible(TrackpadCursor cur, const ViewRect& video, double viewportW,
    double viewportH);

TrackpadCursor MoveCursorBy(TrackpadCursor cur, double dx, double dy, const ViewRect& video,
    double viewportW, double viewportH);

bool CursorScreenPoint(TrackpadCursor cur, const ViewRect& video, double& px, double& py);

bool NormalizeCursor(TrackpadCursor cur, const ViewRect& video, int32_t& nx, int32_t& ny);

}
