#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhubp/system/Clock.h"

#include <cstdio>

namespace {

void TestTheClockNeverGoesBackwards() {
    std::printf("[clock] time only moves forward, so no interval is ever negative...\n");
    uint64_t prev = NowUs();
    Check(prev != 0, "the clock is running");

    for (int i = 0; i < 10'000; ++i) {
        const uint64_t now = NowUs();
        Check(now >= prev, "a later reading is never smaller than an earlier one");
        prev = now;
    }
}

void TestTheClockActuallyAdvances() {
    std::printf("[clock] time moves at roughly the rate it claims...\n");
    const uint64_t t0 = NowUs();
    SleepUs(20'000);
    const uint64_t elapsed = NowUs() - t0;

    Check(elapsed >= 15'000, "20 ms of sleep is measured as at least 15 ms");
    Check(elapsed < 2'000'000,
        "and as well under two seconds, so the unit really is microseconds");
}

void TestTheWallClockIsNotTheMonotonicOne() {
    std::printf("[clock] the wall clock reads a real date, not seconds since boot...\n");
    constexpr int64_t kStartOf2020 = 1'577'836'800;
    constexpr int64_t kStartOf2100 = 4'102'444'800;

    const int64_t now = NowUnixSeconds();
    Check(now > kStartOf2020, "it is well past 2020, so this is not an uptime counter");
    Check(now < kStartOf2100, "and not so far ahead that the units are wrong");

    const int64_t asIfMonotonic = int64_t(NowUs() / 1'000'000);
    Check(asIfMonotonic < kStartOf2020,
        "while the monotonic clock, read the same way, is nowhere near a real date - "
        "which is exactly how it renders as 1970 when stored by mistake");
}

void TestSleepingWaits() {
    std::printf("[sleep] a pacing sleep waits instead of returning straight away...\n");
    const uint64_t t0 = NowUs();
    SleepUs(5'000);
    Check(NowUs() - t0 >= 2'000, "a 5 ms sleep is not optimised into nothing");
}

void TestSleepingForNothingReturnsAtOnce() {
    std::printf("[sleep] a zero-length sleep costs nothing, so pacing can call it freely...\n");
    const uint64_t t0 = NowUs();
    for (int i = 0; i < 1'000; ++i) SleepUs(0);
    Check(NowUs() - t0 < 500'000, "a thousand zero sleeps do not add up to real time");
}

}

void RunClockTests() {
    TestTheClockNeverGoesBackwards();
    TestTheClockActuallyAdvances();
    TestTheWallClockIsNotTheMonotonicOne();
    TestSleepingWaits();
    TestSleepingForNothingReturnsAtOnce();
}
