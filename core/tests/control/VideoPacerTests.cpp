#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/control/VideoPacer.h"

#include <cstdint>
#include <cstdio>

using deskhub::VideoPacer;

namespace {

constexpr uint64_t kLeadUs = VideoPacer::kDefaultLeadUs;
constexpr uint64_t kFrameUs = 16'667;
constexpr uint64_t kTransitUs = 100'000;

VideoPacer SteadyPacer(uint64_t frames, uint64_t& lastPts, uint64_t& lastNow) {
    VideoPacer pacer;
    for (uint64_t i = 0; i < frames; ++i) {
        const uint64_t pts = 1'000'000 + i * kFrameUs;
        const uint64_t jitterUs = (i % 4) * 3'000;
        lastPts = pts;
        lastNow = pts + kTransitUs + jitterUs;
        pacer.ObserveArrival(lastPts, lastNow);
    }
    return pacer;
}

void TestSteadyStreamGetsTheLead() {
    std::printf("[pacer] the fastest frame is scheduled one lead ahead of arrival...\n");
    uint64_t pts = 0, now = 0;
    VideoPacer pacer = SteadyPacer(64, pts, now);
    Check(pacer.ready(), "the pacer is primed after a steady run");

    const uint64_t fastestPts = pts + kFrameUs;
    const uint64_t fastestNow = fastestPts + kTransitUs;
    pacer.ObserveArrival(fastestPts, fastestNow);
    Check(pacer.DisplayTimeUs(fastestPts, fastestNow) == fastestNow + kLeadUs,
        "a zero-jitter frame displays exactly one lead after it arrives");

    const uint64_t jitteredPts = fastestPts + kFrameUs;
    const uint64_t jitteredNow = jitteredPts + kTransitUs + 10'000;
    pacer.ObserveArrival(jitteredPts, jitteredNow);
    Check(pacer.DisplayTimeUs(jitteredPts, jitteredNow) == jitteredNow + kLeadUs - 10'000,
        "a jittered frame spends its slack and keeps the same display cadence");
}

void TestJitterRemovedFromCadence() {
    std::printf("[pacer] display times follow the pts grid, not the arrival wobble...\n");
    uint64_t pts = 0, now = 0;
    VideoPacer pacer = SteadyPacer(64, pts, now);

    uint64_t prevDisplay = 0;
    for (uint64_t i = 1; i <= 4; ++i) {
        const uint64_t p = pts + i * kFrameUs;
        const uint64_t n = p + kTransitUs + (i % 2) * 9'000;
        pacer.ObserveArrival(p, n);
        const uint64_t display = pacer.DisplayTimeUs(p, n);
        if (prevDisplay)
            Check(display - prevDisplay == kFrameUs,
                "consecutive display times are one frame interval apart");
        prevDisplay = display;
    }
}

void TestLateFrameDisplaysImmediately() {
    std::printf("[pacer] a frame later than the lead is shown at once, not queued...\n");
    uint64_t pts = 0, now = 0;
    VideoPacer pacer = SteadyPacer(64, pts, now);

    const uint64_t latePts = pts + kFrameUs;
    const uint64_t lateNow = latePts + kTransitUs + kLeadUs + 20'000;
    pacer.ObserveArrival(latePts, lateNow);
    Check(pacer.DisplayTimeUs(latePts, lateNow) == lateNow,
        "the display time clamps to now instead of the past");
}

void TestResyncThreshold() {
    std::printf("[pacer] the timebase is nudged only past the resync threshold...\n");
    uint64_t pts = 0, now = 0;
    VideoPacer pacer = SteadyPacer(64, pts, now);

    const int64_t desired = pacer.DesiredTimebaseUs(now);
    Check(!pacer.NeedsResync(desired, now), "an exact timebase needs no resync");
    Check(!pacer.NeedsResync(desired + 100'000, now), "a small drift is left to glide");
    Check(pacer.NeedsResync(desired + 300'000, now), "a large forward drift forces a jump");
    Check(pacer.NeedsResync(desired - 300'000, now), "a large backward drift forces a jump");
}

void TestStreamRestartResets() {
    std::printf("[pacer] a pts jump is read as a new stream, not as huge jitter...\n");
    uint64_t pts = 0, now = 0;
    VideoPacer pacer = SteadyPacer(64, pts, now);

    const uint64_t restartPts = 1'000;
    const uint64_t restartNow = now + kFrameUs;
    pacer.ObserveArrival(restartPts, restartNow);
    Check(pacer.ready(), "the pacer reprimes on the first frame of the new stream");
    Check(pacer.DisplayTimeUs(restartPts, restartNow) == restartNow + kLeadUs,
        "the new stream starts on a fresh mapping instead of freezing");
}

void TestResetClears() {
    std::printf("[pacer] reset forgets the mapping...\n");
    uint64_t pts = 0, now = 0;
    VideoPacer pacer = SteadyPacer(8, pts, now);
    pacer.Reset();
    Check(!pacer.ready(), "a reset pacer is unprimed");
}

}

void RunVideoPacerTests() {
    TestSteadyStreamGetsTheLead();
    TestJitterRemovedFromCadence();
    TestLateFrameDisplaysImmediately();
    TestResyncThreshold();
    TestStreamRestartResets();
    TestResetClears();
}
