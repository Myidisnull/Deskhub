#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/input/TrackpadCursor.h"
#include "deskhub/media/ViewFit.h"

#include <cstdio>

using namespace deskhub;

namespace {

constexpr double kEpsilon = 1e-9;

bool Near(double a, double b) {
    const double d = a - b;
    return (d < 0 ? -d : d) < kEpsilon;
}

void TestCursorStartsCentred() {
    std::printf("[cursor] a fresh trackpad puts the pointer in the middle of the picture...\n");
    const TrackpadCursor cur;
    Check(Near(cur.x, 0.5) && Near(cur.y, 0.5), "the default sits at the centre");

    const ViewRect video{0, 0, 800, 600};
    double px = 0, py = 0;
    Check(CursorScreenPoint(cur, video, px, py), "a real rect yields a screen point");
    Check(Near(px, 400) && Near(py, 300), "which is the middle of that rect");

    Check(!CursorScreenPoint(cur, ViewRect{}, px, py), "an empty rect has nowhere to draw");
}

void TestDraggingMovesByAFractionOfTheRect() {
    std::printf("[cursor] a finger drag moves the pointer by its share of the picture...\n");
    const ViewRect video{0, 0, 800, 600};

    const TrackpadCursor moved = MoveCursorBy({0.5, 0.5}, 80, -60, video, 800, 600);
    Check(Near(moved.x, 0.6), "80px across an 800px picture is a tenth");
    Check(Near(moved.y, 0.4), "and 60px up an 600px picture is a tenth back");

    const TrackpadCursor same = MoveCursorBy({0.5, 0.5}, 10, 10, ViewRect{}, 800, 600);
    Check(same == TrackpadCursor{0.5, 0.5}, "with no picture there is nothing to move over");
}

void TestCursorStopsAtTheEdge() {
    std::printf("[cursor] the pointer stops at the edge of the picture, never past it...\n");
    const ViewRect video{0, 0, 800, 600};

    const TrackpadCursor right = MoveCursorBy({0.5, 0.5}, 10'000, 0, video, 800, 600);
    Check(Near(right.x, 1.0), "dragging past the right edge parks it on the edge");

    const TrackpadCursor left = MoveCursorBy({0.5, 0.5}, -10'000, 0, video, 800, 600);
    Check(Near(left.x, 0.0), "and the same on the left");
}

void TestZoomedPictureClampsToWhatIsOnScreen() {
    std::printf("[cursor] when the picture is zoomed past the screen, only the visible part is "
                "reachable...\n");
    const ViewRect video{-400, -300, 1600, 1200};

    const TrackpadCursor clamped = ClampToVisible({0.0, 0.0}, video, 800, 600);
    Check(Near(clamped.x, 0.25), "the left quarter is off-screen, so 0.25 is as far left as it "
                                 "goes");
    Check(Near(clamped.y, 0.25), "same vertically");

    const TrackpadCursor far = ClampToVisible({1.0, 1.0}, video, 800, 600);
    Check(Near(far.x, 0.75) && Near(far.y, 0.75), "and the far corner stops at the visible edge");

    const TrackpadCursor inside = ClampToVisible({0.5, 0.5}, video, 800, 600);
    Check(inside == TrackpadCursor{0.5, 0.5}, "a pointer already on screen is left alone");
}

void TestOffScreenPictureLeavesTheCursorAlone() {
    std::printf("[cursor] a picture nobody can see cannot move the pointer...\n");
    const TrackpadCursor cur{0.3, 0.7};
    Check(ClampToVisible(cur, ViewRect{2000, 0, 800, 600}, 800, 600) == cur,
        "a rect scrolled fully off the right leaves it where it was");
    Check(ClampToVisible(cur, ViewRect{}, 800, 600) == cur, "so does an empty rect");
    Check(ClampToVisible(cur, ViewRect{0, 0, 800, 600}, 0, 0) == cur, "so does an empty viewport");
}

void TestNormalizationMatchesADirectTap() {
    std::printf("[cursor] the trackpad and a direct tap on the same spot send the same "
                "coordinate...\n");
    const ViewRect video{100, 50, 800, 600};

    int32_t nx = 0, ny = 0;
    Check(NormalizeCursor({0.0, 0.0}, video, nx, ny), "the top-left corner normalizes");
    Check(nx == 0 && ny == 0, "and lands on exactly zero");

    Check(NormalizeCursor({1.0, 1.0}, video, nx, ny), "the bottom-right corner normalizes");
    Check(nx == kNormalizedMax && ny == kNormalizedMax, "and lands on exactly the maximum");

    int32_t tapX = 0, tapY = 0;
    Check(NormalizePointer(video.x + 0.25 * video.width, video.y + 0.25 * video.height, video,
              tapX, tapY),
        "a direct tap a quarter in normalizes too");
    Check(NormalizeCursor({0.25, 0.25}, video, nx, ny), "so does the cursor at a quarter");
    Check(nx == tapX && ny == tapY, "and the two agree exactly, so the pointer never jumps");

    Check(!NormalizeCursor({0.5, 0.5}, ViewRect{}, nx, ny), "an empty rect has nothing to send");
}

}

void RunTrackpadCursorTests() {
    TestCursorStartsCentred();
    TestDraggingMovesByAFractionOfTheRect();
    TestCursorStopsAtTheEdge();
    TestZoomedPictureClampsToWhatIsOnScreen();
    TestOffScreenPictureLeavesTheCursorAlone();
    TestNormalizationMatchesADirectTap();
}
