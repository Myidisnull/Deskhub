#include "deskhub/control/FrameGate.h"

namespace deskhub {

bool FrameGate::Admit(uint32_t fps, uint64_t timestampUs) {
    if (!fps) return true;

    const uint64_t stamped = lastUs_.load(std::memory_order_relaxed);
    if (stamped) {
        const uint64_t last = stamped - 1;
        const uint64_t minGapUs = 1'000'000ull / fps;
        if (timestampUs > last && timestampUs - last + kJitterToleranceUs < minGapUs)
            return false;
    }

    lastUs_.store(timestampUs + 1, std::memory_order_relaxed);
    return true;
}

}
