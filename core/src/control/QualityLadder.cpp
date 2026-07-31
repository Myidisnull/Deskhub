#include "deskhub/control/QualityLadder.h"

namespace {

struct Rung {
    uint8_t scalePct;
    uint8_t fps;
};

constexpr Rung kRungs[] = {
    {100, 60},
    {100, 30},
    {100, 20},
    {75, 20},
    {50, 20},
    {50, 12},
};
constexpr int kRungTotal = int(sizeof(kRungs) / sizeof(kRungs[0]));

constexpr uint32_t kMinW = 160, kMinH = 64;

uint16_t ScaleEven(uint16_t v, uint8_t pct) {
    const uint32_t s = (uint32_t(v) * pct) / 100u;
    return uint16_t(s & ~1u);
}

}

namespace deskhub {

QualityLadder::QualityLadder(uint16_t maxW, uint16_t maxH, uint8_t maxFps)
    : maxW_(maxW), maxH_(maxH), maxFps_(maxFps ? maxFps : 60) {
    QualityStep prev{};
    for (int i = 0; i < kRungTotal; ++i) {
        const QualityStep s = StepAt(i);
        if (i > 0) {
            if (s == prev) continue;
            if (s.width < kMinW || s.height < kMinH) break;
        }
        prev = s;
        ++count_;
    }
    step_ = StepAt(0);
}

QualityStep QualityLadder::StepAt(int rung) const {
    if (rung < 0) rung = 0;
    if (rung >= kRungTotal) rung = kRungTotal - 1;
    const Rung& r = kRungs[rung];
    QualityStep s;
    s.width = ScaleEven(maxW_, r.scalePct);
    s.height = ScaleEven(maxH_, r.scalePct);
    s.fps = r.fps < maxFps_ ? r.fps : maxFps_;
    s.scalePct = r.scalePct;
    return s;
}

uint32_t QualityLadder::RequiredBps(int rung) const {
    const QualityStep s = StepAt(rung);
    const uint64_t pixelsPerSec = uint64_t(s.width) * s.height * s.fps;
    return uint32_t((pixelsPerSec * kBppNum) / kBppDen);
}

int QualityLadder::BestRungFor(uint32_t bitrateBps) const {
    for (int i = 0; i < count_; ++i)
        if (bitrateBps >= RequiredBps(i)) return i;
    return count_ - 1;
}

bool QualityLadder::Update(uint32_t bitrateBps, uint64_t nowUs) {
    const int want = BestRungFor(bitrateBps);

    if (want > rung_) {
        rung_ = want;
        step_ = StepAt(rung_);
        lastChangeUs_ = nowUs;
        upSinceUs_ = 0;
        return true;
    }

    if (rung_ == 0) {
        upSinceUs_ = 0;
        return false;
    }

    const int next = rung_ - 1;
    const uint64_t need = (uint64_t(RequiredBps(next)) * kUpHeadroomPct) / 100u;
    if (bitrateBps < need) {
        upSinceUs_ = 0;
        return false;
    }

    if (!upSinceUs_) {
        upSinceUs_ = nowUs;
        return false;
    }

    const QualityStep cand = StepAt(next);
    const bool resize = cand.width != step_.width || cand.height != step_.height;
    const uint64_t dwell = resize ? kUpDwellResizeUs : kUpDwellUs;
    if (nowUs - upSinceUs_ < dwell) return false;
    if (nowUs - lastChangeUs_ < dwell) return false;

    rung_ = next;
    step_ = cand;
    lastChangeUs_ = nowUs;
    upSinceUs_ = 0;
    return true;
}

}
