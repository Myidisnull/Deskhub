#include "deskhub/control/ClockOffset.h"

#include <algorithm>

namespace deskhub {

void ClockOffset::Reset() {
    haveSample_ = false;
    havePrev_ = false;
    curMin_ = prevMin_ = floor_ = lastRaw_ = 0;
    windowStartUs_ = 0;
}

void ClockOffset::Rotate(uint64_t localUs) {
    while (localUs - windowStartUs_ >= kWindowUs) {
        prevMin_ = curMin_;
        havePrev_ = !curEmpty_;
        windowStartUs_ += kWindowUs;
        curMin_ = 0;
        curEmpty_ = true;
    }
}

void ClockOffset::AddSample(uint64_t hostPtsUs, uint64_t localUs) {
    const int64_t raw = int64_t(localUs) - int64_t(hostPtsUs);
    lastRaw_ = raw;

    if (!haveSample_) {
        haveSample_ = true;
        windowStartUs_ = localUs;
        curMin_ = raw;
        curEmpty_ = false;
        havePrev_ = false;
        floor_ = raw;
        return;
    }

    Rotate(localUs);

    if (curEmpty_) {
        curMin_ = raw;
        curEmpty_ = false;
    } else {
        curMin_ = std::min(curMin_, raw);
    }

    floor_ = havePrev_ ? std::min(curMin_, prevMin_) : curMin_;
}

int64_t ClockOffset::LatencyUs(uint64_t netFloorUs) const {
    if (!haveSample_) return -1;
    const int64_t excess = std::max<int64_t>(0, lastRaw_ - floor_);
    return excess + int64_t(netFloorUs);
}

}
