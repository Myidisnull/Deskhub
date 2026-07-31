#pragma once
#include <cstdint>

namespace deskhub {

class ClockOffset {
public:
    static constexpr uint64_t kWindowUs = 10'000'000;

    void AddSample(uint64_t hostPtsUs, uint64_t localUs);

    bool ready() const {
        return haveSample_;
    }

    int64_t LatencyUs(uint64_t netFloorUs = 0) const;

    int64_t floorUs() const {
        return floor_;
    }

    void Reset();

private:
    void Rotate(uint64_t localUs);

    bool haveSample_ = false;
    int64_t curMin_ = 0;
    int64_t prevMin_ = 0;
    bool curEmpty_ = true;
    bool havePrev_ = false;
    int64_t floor_ = 0;
    int64_t lastRaw_ = 0;
    uint64_t windowStartUs_ = 0;
};

}
