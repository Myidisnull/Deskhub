#include "deskhub/control/FrameGate.h"

namespace deskhub {

bool FrameGate::Admit(uint32_t fps, uint64_t timestampUs) {
    if (!fps) return true;

    const uint64_t minGapUs = 1'000'000ull / fps;
    const uint64_t stamped = lastUs_.load(std::memory_order_relaxed);
    const bool haveReference = stamped != 0 && timestampUs > stamped - 1;

    if (haveReference) {
        const uint64_t dueUs = nextDueUs_.load(std::memory_order_relaxed);
        if (timestampUs + kJitterToleranceUs < dueUs) return false;
        const uint64_t carried = dueUs + minGapUs;
        nextDueUs_.store(carried < timestampUs ? timestampUs + minGapUs : carried,
            std::memory_order_relaxed);
    } else {
        nextDueUs_.store(timestampUs + minGapUs, std::memory_order_relaxed);
    }

    lastUs_.store(timestampUs + 1, std::memory_order_relaxed);
    return true;
}

}
