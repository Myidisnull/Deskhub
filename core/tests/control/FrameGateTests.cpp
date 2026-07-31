#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/control/FrameGate.h"

#include <cstdio>

using namespace deskhub;

namespace {

void TestUngated() {
    std::printf("[gate] fps 0 means every frame is admitted...\n");
    FrameGate gate;
    Check(gate.Admit(0, 1000), "first frame passes");
    Check(gate.Admit(0, 1001), "a frame 1us later still passes");
    Check(!gate.hasReference(), "an ungated gate keeps no reference");
}

void TestFirstFrameAlwaysPasses() {
    std::printf("[gate] the first frame is never dropped...\n");
    FrameGate gate;
    Check(gate.Admit(30, 5'000'000), "no previous frame -> admitted");
    Check(gate.lastAdmittedUs() == 5'000'000, "and it becomes the reference");
}

void TestDropsTooEarly() {
    std::printf("[gate] a frame arriving early for the target fps is dropped...\n");
    FrameGate gate;
    Check(gate.Admit(30, 0), "reference frame");
    Check(!gate.Admit(30, 10'000), "10ms after at 30fps (33.3ms budget) -> dropped");
    Check(gate.lastAdmittedUs() == 0, "a dropped frame does not move the reference");
    Check(gate.Admit(30, 33'333), "a full period later -> admitted");
    Check(gate.lastAdmittedUs() == 33'333, "the reference advances");
}

void TestJitterTolerance() {
    std::printf("[gate] a frame a hair early still counts, so 60fps does not halve...\n");
    FrameGate gate;
    Check(gate.Admit(60, 0), "reference frame");
    Check(gate.Admit(60, 16'300), "16.3ms at 60fps (16.6ms budget) is within tolerance");

    FrameGate strict;
    Check(strict.Admit(60, 0), "reference frame");
    Check(!strict.Admit(60, 15'000), "15ms is more than the tolerance early -> dropped");
}

void TestNonMonotonicTimestamps() {
    std::printf("[gate] a timestamp that goes backwards is admitted, not swallowed...\n");
    FrameGate gate;
    Check(gate.Admit(30, 1'000'000), "reference frame");
    Check(gate.Admit(30, 900'000), "an older timestamp passes instead of wedging the gate");
    Check(gate.lastAdmittedUs() == 900'000, "and it resets the reference");
}

void TestReset() {
    std::printf("[gate] Reset drops the reference so the next frame passes...\n");
    FrameGate gate;
    Check(gate.Admit(30, 0), "reference frame");
    Check(!gate.Admit(30, 1'000), "next frame is too early");
    gate.Reset();
    Check(!gate.hasReference(), "Reset clears the reference");
    Check(gate.Admit(30, 1'000), "after Reset the same frame is admitted");
}

void TestZeroTimestampIsAReference() {
    std::printf("[gate] a timestamp of exactly 0 still counts as a reference...\n");
    FrameGate gate;
    Check(gate.Admit(30, 0), "the frame at t=0 is admitted");
    Check(gate.hasReference(), "and it is remembered, not mistaken for an empty gate");
    Check(!gate.Admit(30, 1'000), "so the next frame 1ms later is dropped");
}

}

void RunFrameGateTests() {
    TestUngated();
    TestFirstFrameAlwaysPasses();
    TestDropsTooEarly();
    TestJitterTolerance();
    TestNonMonotonicTimestamps();
    TestReset();
    TestZeroTimestampIsAReference();
}
