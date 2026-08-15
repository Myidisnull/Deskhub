#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/session/AuthThrottle.h"

#include <cstdio>

using namespace deskhub;

namespace {

constexpr uint64_t kStartUs = 1'000'000;

void TestThreeFailuresLockTheDoor() {
    std::printf("[throttle] three wrong guesses shut the door for the lockout window...\n");
    AuthThrottle throttle;
    Check(!throttle.Locked(kStartUs), "a fresh throttle is open");

    throttle.RecordFailure(kStartUs);
    throttle.RecordFailure(kStartUs + 1);
    Check(!throttle.Locked(kStartUs + 2), "two wrong guesses do not lock it yet");

    throttle.RecordFailure(kStartUs + 2);
    Check(throttle.Locked(kStartUs + 3), "the third one does");
    Check(throttle.Locked(kStartUs + 2 + kPasscodeLockoutUs - 1),
        "and it stays shut for the whole window");
    Check(!throttle.Locked(kStartUs + 2 + kPasscodeLockoutUs),
        "then opens again on its own");
}

void TestTheCountRestartsAfterALockout() {
    std::printf("[throttle] a served lockout does not shorten the next one...\n");
    AuthThrottle throttle;
    for (uint32_t i = 0; i < kMaxPasscodeAttempts; ++i) throttle.RecordFailure(kStartUs);
    const uint64_t reopenedUs = kStartUs + kPasscodeLockoutUs;
    Check(!throttle.Locked(reopenedUs), "the door reopened");

    throttle.RecordFailure(reopenedUs);
    Check(!throttle.Locked(reopenedUs + 1),
        "and one more wrong guess after it is only the first of a new count");
}

void TestSuccessResetsTheCount() {
    std::printf("[throttle] a correct passcode wipes the slate...\n");
    AuthThrottle throttle;
    throttle.RecordFailure(kStartUs);
    throttle.RecordFailure(kStartUs + 1);
    throttle.RecordSuccess();

    throttle.RecordFailure(kStartUs + 2);
    throttle.RecordFailure(kStartUs + 3);
    Check(!throttle.Locked(kStartUs + 4), "two guesses after a success do not lock");

    throttle.RecordFailure(kStartUs + 4);
    Check(throttle.Locked(kStartUs + 5), "three in a row still do");
}

}

void RunAuthThrottleTests() {
    TestThreeFailuresLockTheDoor();
    TestTheCountRestartsAfterALockout();
    TestSuccessResetsTheCount();
}
