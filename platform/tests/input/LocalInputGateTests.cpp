#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhubp/input/LocalInputGate.h"
#include "deskhubp/system/Clock.h"

#include <cstdio>

namespace {

struct Injector : deskhubp::LocalInputGate<Injector> {
    int releases = 0;
    void ReleaseAll() {
        ++releases;
    }
};

void TestInjectionIsOnUntilSomeoneTurnsItOff() {
    std::printf("[gate] a fresh host accepts remote input without being told to...\n");
    Injector inj;
    Check(inj.enabled(), "sharing starts with input enabled, or nothing would respond");
    Check(inj.releases == 0, "and nothing was released on the way there");
}

void TestDisablingReleasesWhatIsStillHeld() {
    std::printf("[gate] turning injection off lets go of every key still down...\n");
    Injector inj;
    inj.SetEnabled(false);
    Check(!inj.enabled(), "injection is off");
    Check(inj.releases == 1,
        "the held keys were released, so the host is not left with ctrl stuck down");
}

void TestRedundantChangesDoNothing() {
    std::printf("[gate] setting the state it is already in does not release anything...\n");
    Injector inj;
    inj.SetEnabled(true);
    Check(inj.releases == 0, "enabling an enabled gate is a no-op");

    inj.SetEnabled(false);
    Check(inj.releases == 1, "the first disable releases");
    inj.SetEnabled(false);
    inj.SetEnabled(false);
    Check(inj.releases == 1,
        "repeating it does not release again, which would fight with the local user");
}

void TestReEnablingDoesNotRelease() {
    std::printf("[gate] turning injection back on does not disturb the local keyboard...\n");
    Injector inj;
    inj.SetEnabled(false);
    inj.SetEnabled(true);
    Check(inj.enabled(), "injection is back on");
    Check(inj.releases == 1, "only the disable released; enabling touches nothing");
}

void TestWithoutAMonitorNobodyIsAssumedToBeAtTheKeyboard() {
    std::printf("[gate] with no local monitor the remote user is never locked out...\n");
    Injector inj;
    Check(!inj.localUserActive(),
        "no monitor means no evidence of a local user, so remote input is not blocked");
}

void TestAMonitorThatHasSeenNothingReportsNobody() {
    std::printf("[gate] a monitor that has recorded no local input reports nobody...\n");
    Injector inj;
    LocalInputMonitor monitor;
    inj.SetLocalMonitor(&monitor);
    Check(!inj.localUserActive(),
        "a monitor that was never started has no last-input time to report");

    inj.SetLocalMonitor(nullptr);
    Check(!inj.localUserActive(), "and detaching it goes back to 'nobody'");
}

void TestTheQuietWindowIsAWholeSecond() {
    std::printf("[gate] the local user keeps priority for a second after their last key...\n");
    Check(LocalInputMonitor::kQuietUs == 1'000'000,
        "one second: long enough to type, short enough not to strand the remote user");
}

}

void RunLocalInputGateTests() {
    TestInjectionIsOnUntilSomeoneTurnsItOff();
    TestDisablingReleasesWhatIsStillHeld();
    TestRedundantChangesDoNothing();
    TestReEnablingDoesNotRelease();
    TestWithoutAMonitorNobodyIsAssumedToBeAtTheKeyboard();
    TestAMonitorThatHasSeenNothingReportsNobody();
    TestTheQuietWindowIsAWholeSecond();
}
