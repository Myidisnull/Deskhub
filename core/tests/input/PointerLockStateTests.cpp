#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/input/PointerLockState.h"
#include "deskhub/input/Set1Scancodes.h"
#include "deskhub/ui/Brand.h"

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
    Check(!st.OnEscape().lockChanged, "Escape with no lock is left to the remote desktop");

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

void TestHintAndTitleFollowTheState() {
    std::printf("[lock] the title always carries the status line and the F9 hint...\n");
    PointerLockState st;
    Check(st.HintText() == kViewerLockHint, "an unlocked viewer advertises how to lock");
    st.OnToggleLockKey();
    Check(st.HintText() == kViewerLockedHint, "a locked one advertises how to release");

    const std::string title =
        st.TitleFor(std::string(deskhub::brand::kProductName) + " - viewing: Display 1", "12 fps");
    Check(title.find("12 fps") != std::string::npos, "the status line is in the title");
    Check(title.find(kViewerLockedHint) != std::string::npos, "so is the lock hint");

    const std::string subtitle = st.SubtitleFor("12 fps");
    Check(subtitle.find("12 fps") != std::string::npos &&
              subtitle.find(kViewerLockedHint) != std::string::npos,
        "the subtitle form carries both too");
}

void TestSeededState() {
    std::printf("[lock] a restored viewer starts from the state it saved...\n");
    const PointerLockState st(true);
    Check(st.locked(), "restored as locked");
    Check(st.HintText() == kViewerLockedHint, "and hints accordingly");
}

void TestLockToggleKeyIsCanonical() {
    std::printf("[lock] every platform agrees on which key toggles the lock...\n");
    Check(kViewerLockToggleVk == kVkF1 + 8, "the toggle key is F9 in the shared VK space");
    Check(VkToSet1Scancode(kViewerLockToggleVk) != 0,
        "and F9 has a real scancode, so it could be forwarded if it were not consumed");
}

}

void RunPointerLockStateTests() {
    TestLockTogglesAndReports();
    TestSeededState();
    TestEscapeOnlyReleasesWhenLocked();
    TestFocusLossAlwaysReleasesHeldInput();
    TestHintAndTitleFollowTheState();
    TestLockToggleKeyIsCanonical();
}
