#pragma once
#include <cstdint>

#include "deskhub/control/ClockOffset.h"

namespace deskhub {

class VideoPacer {
public:
    static constexpr uint64_t kDefaultLeadUs = 33'000;
    static constexpr uint64_t kResyncThresholdUs = 250'000;
    static constexpr uint64_t kStreamJumpUs = 2'000'000;

    explicit VideoPacer(uint64_t leadUs = kDefaultLeadUs) : leadUs_(leadUs) {}

    void ObserveArrival(uint64_t ptsUs, uint64_t nowUs);

    bool ready() const {
        return offset_.ready();
    }

    int64_t DesiredTimebaseUs(uint64_t nowUs) const;

    uint64_t DisplayTimeUs(uint64_t ptsUs, uint64_t nowUs) const;

    bool NeedsResync(int64_t currentTimebaseUs, uint64_t nowUs) const;

    void Reset() {
        offset_.Reset();
    }

private:
    ClockOffset offset_;
    uint64_t leadUs_;
};

}
