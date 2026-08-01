#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/input/PointerLockState.h"

#include <cstdio>

using namespace deskhub;

namespace {

void TestLockTogglesAndReports() {
    std::printf("[lock] the toggle key flips the lock and says so...\n");
    PointerLockState st;
    Check(!st.locked(), "a fresh viewer does not hold the pointer");

    const PointerLockEffect on = st.OnToggleLockKey();
    Check(st.locked() && on.lockChanged, "the first press locks");
    Check(!on.releaseHeldInput, "locking does not drop what the user is holding");

    Check(st.OnToggleLockKey().lockChanged, "the second press reports a change too");
    Check(!st.locked(), "and unlocks");
}

void TestEscapeOnlyReleasesWhenLocked() {
    std::printf("[lock] Escape releases a locked pointer and is otherwise not ours...\n");
    PointerLockState st;
    Check(!st.OnEscape().anyChange(), "Escape with no lock is left to the remote desktop");

    st.OnToggleLockKey();
    Check(st.OnEscape().lockChanged, "Escape while locked releases");
    Check(!st.locked(), "so the pointer is free again");
}

void TestFocusLossAlwaysReleasesHeldInput() {
    std::printf("[lock] losing focus drops held keys, and the lock if there was one...\n");
    PointerLockState st;

    const PointerLockEffect idle = st.OnFocusLost();
    Check(idle.releaseHeldInput, "held input is released even when nothing was locked");
    Check(!idle.lockChanged, "but there was no lock to report");

    st.OnToggleLockKey();
    const PointerLockEffect held = st.OnFocusLost();
    Check(held.releaseHeldInput && held.lockChanged, "with a lock, both happen");
    Check(!st.locked(), "and the pointer is handed back to the local desktop");
}

void TestPauseGatesInputAndReleases() {
    std::printf("[lock] pausing gates input and releases what is held; resuming does not...\n");
    PointerLockState st;
    Check(st.acceptsInput(), "input flows by default");

    const PointerLockEffect paused = st.OnTogglePauseKey();
    Check(st.paused() && paused.pauseChanged, "the pause key pauses");
    Check(!st.acceptsInput(), "so nothing is forwarded");
    Check(paused.releaseHeldInput, "and whatever was held is released, not left latched");

    const PointerLockEffect resumed = st.OnTogglePauseKey();
    Check(!st.paused() && st.acceptsInput(), "pressing again resumes");
    Check(!resumed.releaseHeldInput, "resuming has nothing to release");
}

void TestPauseAndLockAreIndependent() {
    std::printf("[lock] pausing input does not release the pointer, and vice versa...\n");
    PointerLockState st;
    st.OnToggleLockKey();
    st.OnTogglePauseKey();
    Check(st.locked() && st.paused(), "both can be on at once");
    st.OnTogglePauseKey();
    Check(st.locked(), "resuming input leaves the pointer locked");
}

void TestHintAndTitleFollowTheState() {
    std::printf("[lock] the title carries the lock hint, and the pause note only when paused...\n");
    PointerLockState st;
    Check(st.HintText() == kViewerLockHint, "an unlocked viewer advertises how to lock");
    st.OnToggleLockKey();
    Check(st.HintText() == kViewerLockedHint, "a locked one advertises how to release");

    const std::string title = st.TitleFor("Deskhub - viewing: Display 1", "12 fps");
    Check(title.find("12 fps") != std::string::npos, "the status line is in the title");
    Check(title.find(kViewerLockedHint) != std::string::npos, "so is the lock hint");
    Check(title.find(kViewerPauseHint) == std::string::npos, "and no pause note while running");

    st.OnTogglePauseKey();
    Check(st.TitleFor("Deskhub", "12 fps").find(kViewerPauseHint) != std::string::npos,
        "pausing adds the note");
    Check(st.SubtitleFor("12 fps").find(kViewerPauseHint) != std::string::npos,
        "the subtitle form carries it too");
}

}

void RunPointerLockStateTests() {
    TestLockTogglesAndReports();
    TestEscapeOnlyReleasesWhenLocked();
    TestFocusLossAlwaysReleasesHeldInput();
    TestPauseGatesInputAndReleases();
    TestPauseAndLockAreIndependent();
    TestHintAndTitleFollowTheState();
}
