#pragma once
#include <cstdint>

namespace deskhub::media {

struct RatePlan {
    uint32_t fps = 0;
    uint32_t frameBits = 0;
    uint32_t vbvBits = 0;
    uint32_t vbvInitialBits = 0;
};

RatePlan PlanRateControl(uint32_t bitrateBps, uint32_t fps, bool lowLatency);

}
