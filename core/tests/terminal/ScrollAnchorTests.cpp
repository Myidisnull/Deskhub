#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/terminal/ScrollAnchor.h"

#include <climits>
#include <cstdio>

using deskhub::term::AnchorScroll;
using deskhub::term::ScrollByRows;

namespace {

void TestAtTheBottomNewOutputDoesNotMoveTheView() {
    std::printf("[scroll] sitting at the bottom, new output just arrives...\n");
    Check(AnchorScroll(0, 40, 60) == 0, "an offset of zero stays at the live tail");
    Check(AnchorScroll(0, 0, 0) == 0, "and an empty scrollback changes nothing");
}

void TestScrolledUpTheSameLinesStayInView() {
    std::printf("[scroll] scrolled up, the lines under the cursor keep their place...\n");
    Check(AnchorScroll(10, 40, 60) == 30, "twenty new rows push the offset twenty further back");
    Check(AnchorScroll(10, 40, 41) == 11, "one new row shifts by one");
    Check(AnchorScroll(10, 40, 40) == 10, "no new rows leaves the offset alone");
}

void TestScrollbackTrimDoesNotShiftTheView() {
    std::printf("[scroll] a scrollback that shrank never drags the view forward...\n");
    Check(AnchorScroll(10, 60, 40) == 10, "a smaller scrollback leaves the offset where it was");
}

void TestScrollingUpStopsAtTheOldestRow() {
    std::printf("[scroll] scrolling up stops at the oldest row kept...\n");
    Check(ScrollByRows(0, 3, 100) == 3, "three rows up from the bottom");
    Check(ScrollByRows(98, 3, 100) == 100, "and it clamps to the top of the scrollback");
    Check(ScrollByRows(0, 5, 0) == 0, "with nothing scrolled back there is nowhere to go");
}

void TestScrollingDownStopsAtTheLiveTail() {
    std::printf("[scroll] scrolling down stops at the live tail...\n");
    Check(ScrollByRows(10, -3, 100) == 7, "three rows down");
    Check(ScrollByRows(2, -9, 100) == 0, "and it never goes past the bottom");
    Check(ScrollByRows(0, -9, 100) == 0, "already at the bottom, down does nothing");
}

void TestAnOffsetPastTheEndIsPulledBack() {
    std::printf("[scroll] an offset beyond what is kept is pulled back into range...\n");
    Check(ScrollByRows(500, 0, 100) == 100, "a stale offset clamps to the scrollback");
    Check(ScrollByRows(500, -1, 100) == 100, "even while scrolling down toward it");
}

void TestExtremeRowCountsDoNotOverflow() {
    std::printf("[scroll] a wheel event with an absurd row count stays in range...\n");
    Check(ScrollByRows(10, INT_MIN, 100) == 0, "the largest step down lands at the bottom");
    Check(ScrollByRows(10, INT_MAX, 100) == 100, "the largest step up lands at the top");
}

}

void RunScrollAnchorTests() {
    TestAtTheBottomNewOutputDoesNotMoveTheView();
    TestScrolledUpTheSameLinesStayInView();
    TestScrollbackTrimDoesNotShiftTheView();
    TestScrollingUpStopsAtTheOldestRow();
    TestScrollingDownStopsAtTheLiveTail();
    TestAnOffsetPastTheEndIsPulledBack();
    TestExtremeRowCountsDoNotOverflow();
}
