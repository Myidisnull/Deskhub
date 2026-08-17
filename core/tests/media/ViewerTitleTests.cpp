#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/media/ViewerTitle.h"
#include "deskhub/ui/Brand.h"

#include <cstdio>
#include <string>

using namespace deskhub;

namespace {

bool Contains(const std::string& haystack, std::string_view needle) {
    return haystack.find(needle) != std::string::npos;
}

std::string ViewingBase() {
    return std::string(brand::kProductName) + " - viewing";
}

void TestLockHintFollowsTheLockState() {
    std::printf("[title] the hint always tells the user what F9 will do next...\n");
    Check(ViewerLockHintText(false) == kViewerLockHint, "unlocked: offers to lock");
    Check(ViewerLockHintText(true) == kViewerLockedHint, "locked: offers to release");
    Check(ViewerLockHintText(true) != ViewerLockHintText(false),
        "the two states never read the same");
    Check(Contains(std::string(kViewerLockHint), "F9") &&
              Contains(std::string(kViewerLockedHint), "F9"),
        "both hints name the key, so the user never has to guess");
}

void TestBaseTitleNamesTheSource() {
    std::printf("[title] the window title says what is being viewed...\n");
    Check(ViewerBaseTitle("Display 1") == ViewingBase() + ": Display 1",
        "a known source is named");
    Check(ViewerBaseTitle("") == ViewingBase(),
        "before the host answers there is no dangling separator");
}

void TestStatusFallsBackToConnecting() {
    std::printf("[title] an empty status reads as connecting, never as a blank gap...\n");
    const std::string s = ViewerStatusWithHint("", false);
    Check(Contains(s, kViewerConnectingStatus), "empty status is replaced by 'connecting...'");
    Check(Contains(s, kViewerLockHint), "the hint is still there while connecting");

    const std::string live = ViewerStatusWithHint("60fps  12ms", true);
    Check(Contains(live, "60fps  12ms"), "a real status line is passed through untouched");
    Check(Contains(live, kViewerLockedHint), "the hint tracks the lock state");
    Check(!Contains(live, kViewerConnectingStatus),
        "a real status is never mixed with the placeholder");
}

void TestViewOnlySubtitleDropsTheLockHint() {
    std::printf("[title] a view-only session never promises a mouse lock...\n");
    const std::string viewOnly = ViewerViewOnlySubtitle("60fps  12ms");
    Check(Contains(viewOnly, "60fps  12ms"), "the status line is kept");
    Check(Contains(viewOnly, kViewerViewOnlyHint), "the subtitle says the session is view-only");
    Check(!Contains(viewOnly, kViewerLockHint) && !Contains(viewOnly, kViewerLockedHint),
        "neither F9 hint appears, since input is never sent");
    Check(Contains(ViewerViewOnlySubtitle(""), kViewerConnectingStatus),
        "an empty status still reads as connecting");
}

void TestComposedTitleKeepsAllThreeParts() {
    std::printf("[title] base, status and hint all survive into the final title...\n");
    const std::string title =
        ComposeViewerTitle(ViewerBaseTitle("Display 1"), "60fps", kViewerLockHint);
    Check(Contains(title, ViewingBase() + ": Display 1"), "the base is kept");
    Check(Contains(title, "60fps"), "the status is kept");
    Check(Contains(title, kViewerLockHint), "the hint is kept");
    Check(title.find(brand::kProductName) < title.find("60fps") &&
              title.find("60fps") < title.find(kViewerLockHint),
        "they appear in a stable order, so the title does not reshuffle every second");

    Check(Contains(ComposeViewerTitle(brand::kProductName, "", kViewerLockHint),
              kViewerConnectingStatus),
        "the composed title uses the same connecting placeholder");

    const std::string viewOnly =
        ComposeViewerTitle(brand::kProductName, "60fps", kViewerViewOnlyHint);
    Check(Contains(viewOnly, kViewerViewOnlyHint),
        "a view-only session says so instead of promising a mouse lock");
    Check(!Contains(viewOnly, kViewerLockHint), "and drops the F9 hint that would be a lie");
}

}

void RunViewerTitleTests() {
    TestLockHintFollowsTheLockState();
    TestBaseTitleNamesTheSource();
    TestStatusFallsBackToConnecting();
    TestViewOnlySubtitleDropsTheLockHint();
    TestComposedTitleKeepsAllThreeParts();
}
