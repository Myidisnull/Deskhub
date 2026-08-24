#include "deskhub/ui/AutoShareGate.h"

#include <algorithm>

namespace deskhub::ui {

AutoShareGate::AutoShareGate(uint32_t probeMs, uint32_t giveUpMs)
    : probeMs_(std::max(probeMs, 1u)), giveUpMs_(giveUpMs) {}

AutoShareStep NextAutoShareStep(bool displaysReady, uint32_t waitedMs, uint32_t probeMs,
    uint32_t giveUpMs) {
    if (displaysReady) return AutoShareStep::ShareNow;
    if (waitedMs + std::max(probeMs, 1u) >= giveUpMs) return AutoShareStep::GiveUpWaiting;
    return AutoShareStep::KeepWaiting;
}

AutoShareStep AutoShareGate::Advance(bool displaysReady) {
    if (decided_) return decision_;
    const AutoShareStep step = NextAutoShareStep(displaysReady, waitedMs_, probeMs_, giveUpMs_);
    if (step != AutoShareStep::KeepWaiting) return Decide(step);
    waitedMs_ += probeMs_;
    return step;
}

AutoShareStep AutoShareGate::Decide(AutoShareStep step) {
    decided_ = true;
    decision_ = step;
    return step;
}

}
