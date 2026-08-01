#include "deskhub/media/RatePlan.h"

namespace deskhub::media {

RatePlan PlanRateControl(uint32_t bitrateBps, uint32_t fps, bool lowLatency) {
    RatePlan p;
    p.fps = fps ? fps : 60;
    p.frameBits = bitrateBps / p.fps;
    if (lowLatency) {
        p.vbvBits = p.frameBits * 2;
        p.vbvInitialBits = p.frameBits;
    } else {
        p.vbvBits = bitrateBps;
        p.vbvInitialBits = bitrateBps / 2;
    }
    return p;
}

}
