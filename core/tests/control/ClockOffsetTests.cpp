#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/control/ClockOffset.h"

#include <cstdio>

using namespace deskhub;

namespace {

constexpr int64_t kSkewFresh = 0;
constexpr int64_t kSkewStale = -3ll * 24 * 3600 * 1'000'000;

void Feed(ClockOffset& co, int64_t skew, uint64_t hostUs, uint64_t delayUs) {
    co.AddSample(hostUs, uint64_t(int64_t(hostUs) + int64_t(delayUs) + skew));
}

void TestFloorIsSubtracted(int64_t skew) {
    ClockOffset co;
    Feed(co, skew, 1'000'000, 20'000);
    Check(co.LatencyUs() == 0, "the first frame defines the floor -> 0");
    Feed(co, skew, 1'016'000, 22'000);
    Check(co.LatencyUs() == 2'000, "a frame 2 ms above the floor");
    Feed(co, skew, 1'032'000, 70'000);
    Check(co.LatencyUs() == 50'000, "a 50 ms queueing spike shows up in full");
    Feed(co, skew, 1'048'000, 20'000);
    Check(co.LatencyUs() == 0, "back to the best level -> 0");
}

void TestNetFloorAddedBack(int64_t skew) {
    ClockOffset co;
    Feed(co, skew, 1'000'000, 20'000);
    Feed(co, skew, 1'016'000, 45'000);
    Check(co.LatencyUs() == 25'000, "floor not added: only the excess");
    Check(co.LatencyUs(4'000) == 29'000, "the measured network floor is added back (minRtt/2)");
    Check(co.LatencyUs(0) == 25'000, "a floor of 0 behaves like passing nothing");
}

void TestFloorDecays(int64_t skew) {
    ClockOffset co;
    uint64_t t = 1'000'000;
    Feed(co, skew, t, 20'000);

    for (int i = 0; i < 40; ++i) {
        t += ClockOffset::kWindowUs / 10;
        Feed(co, skew, t, 120'000);
    }
    Check(co.LatencyUs() == 0, "the floor relearned the new level -> a frame at that level is 0");

    t += ClockOffset::kWindowUs / 10;
    Feed(co, skew, t, 20'000);
    Check(co.LatencyUs() == 0, "faster than the current floor still clamps to 0, never negative");
}

void TestLongSilence(int64_t skew) {
    ClockOffset co;
    Feed(co, skew, 1'000'000, 20'000);
    const uint64_t t = 1'000'000 + ClockOffset::kWindowUs * 6;
    Feed(co, skew, t, 20'000);
    Check(co.LatencyUs() == 0, "after a long silence the first sample redefines the floor");
    Feed(co, skew, t + 16'000, 35'000);
    Check(co.LatencyUs() == 15'000, "and the next sample measures exactly the excess");
}

void TestNotReady() {
    ClockOffset co;
    Check(!co.ready(), "no samples yet");
    Check(co.LatencyUs() == -1, "not ready -> -1, not 0");
    co.AddSample(1'000'000, 1'020'000);
    Check(co.ready(), "one sample makes it ready");
    co.Reset();
    Check(!co.ready(), "Reset forgets everything (a new session carries a different C)");
}

void BothSkews(void (*fn)(int64_t), const char* name) {
    std::printf("[clock] %s...\n", name);
    fn(kSkewFresh);
    fn(kSkewStale);
}

}

void RunClockOffsetTests() {
    BothSkews(TestFloorIsSubtracted, "the floor is subtracted, the queueing shows through");
    BothSkews(TestNetFloorAddedBack, "the measured network floor is added back, never counted twice");
    BothSkews(TestFloorDecays, "the floor relearns when the link degrades");
    BothSkews(TestLongSilence, "an idle source silent for several windows");
    std::printf("[clock] no samples / Reset...\n");
    TestNotReady();
}
