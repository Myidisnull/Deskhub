#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/media/RatePlan.h"

#include <cstdio>

using namespace deskhub::media;

namespace {

void TestLowLatencyPlan() {
    std::printf("[rate] the low-latency plan keeps the VBV a couple of frames deep...\n");
    const RatePlan p = PlanRateControl(20'000'000, 60, true);
    Check(p.fps == 60, "the fps is taken as given");
    Check(p.frameBits == 20'000'000 / 60, "one frame's worth of bits");
    Check(p.vbvBits == p.frameBits * 2, "the buffer holds two frames");
    Check(p.vbvInitialBits == p.frameBits, "and starts one frame full");
}

void TestQualityPlan() {
    std::printf("[rate] without low latency the VBV stretches to a full second...\n");
    const RatePlan p = PlanRateControl(20'000'000, 60, false);
    Check(p.vbvBits == 20'000'000, "one second of buffer");
    Check(p.vbvInitialBits == 10'000'000, "half full at start");
}

void TestFpsFallback() {
    std::printf("[rate] a zero fps falls back to 60 instead of dividing by zero...\n");
    const RatePlan p = PlanRateControl(6'000'000, 0, true);
    Check(p.fps == 60, "60 is the fallback");
    Check(p.frameBits == 100'000, "and the frame budget follows it");
}

}

void RunRatePlanTests() {
    TestLowLatencyPlan();
    TestQualityPlan();
    TestFpsFallback();
}
