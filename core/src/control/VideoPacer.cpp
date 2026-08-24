#include "deskhub/control/VideoPacer.h"

namespace deskhub {

void VideoPacer::ObserveArrival(uint64_t ptsUs, uint64_t nowUs) {
    if (offset_.ready()) {
        const int64_t raw = int64_t(nowUs) - int64_t(ptsUs);
        const int64_t drift = raw - offset_.floorUs();
        if (drift > int64_t(kStreamJumpUs) || drift < -int64_t(kStreamJumpUs)) offset_.Reset();
    }
    offset_.AddSample(ptsUs, nowUs);
}

int64_t VideoPacer::DesiredTimebaseUs(uint64_t nowUs) const {
    return int64_t(nowUs) - offset_.floorUs() - int64_t(leadUs_);
}

uint64_t VideoPacer::DisplayTimeUs(uint64_t ptsUs, uint64_t nowUs) const {
    const int64_t at = int64_t(ptsUs) + offset_.floorUs() + int64_t(leadUs_);
    return at > int64_t(nowUs) ? uint64_t(at) : nowUs;
}

bool VideoPacer::NeedsResync(int64_t currentTimebaseUs, uint64_t nowUs) const {
    const int64_t diff = currentTimebaseUs - DesiredTimebaseUs(nowUs);
    return diff > int64_t(kResyncThresholdUs) || diff < -int64_t(kResyncThresholdUs);
}

}
