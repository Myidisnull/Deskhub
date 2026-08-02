#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/media/ViewFit.h"

#include <cmath>
#include <cstdio>

using namespace deskhub;

namespace {

bool Near(double a, double b, double slop = 0.001) {
    return std::fabs(a - b) <= slop;
}

void TestLetterboxing() {
    std::printf("[viewfit] a 16:9 video is letterboxed into whatever viewport it gets...\n");
    const double aspect = 16.0 / 9.0;

    const ViewRect wide = FitVideoRect(1600, 1200, aspect);
    Check(Near(wide.width, 1600) && Near(wide.height, 900), "a tall viewport pillarboxes nothing");
    Check(Near(wide.x, 0) && Near(wide.y, 150), "and centres the video vertically");

    const ViewRect tall = FitVideoRect(1600, 400, aspect);
    Check(Near(tall.height, 400) && Near(tall.width, 400 * aspect),
        "a short viewport limits by height instead");
    Check(Near(tall.y, 0) && Near(tall.x, (1600 - 400 * aspect) / 2), "centred horizontally");

    const ViewRect exact = FitVideoRect(1920, 1080, aspect);
    Check(Near(exact.x, 0) && Near(exact.y, 0) && Near(exact.width, 1920) &&
              Near(exact.height, 1080),
        "an exactly matching viewport is filled edge to edge");
}

void TestDegenerateInputs() {
    std::printf("[viewfit] a viewport or aspect of zero yields nothing to point at...\n");
    Check(FitVideoRect(0, 100, 1.7).width == 0, "zero width viewport");
    Check(FitVideoRect(100, 0, 1.7).width == 0, "zero height viewport");

    const ViewRect noAspect = FitVideoRect(800, 600, 0);
    Check(Near(noAspect.width, 800) && Near(noAspect.height, 600),
        "an unknown aspect fills the viewport rather than collapsing");
}

void TestZoomIsCentredAndBounded() {
    std::printf("[viewfit] zoom grows the rect about the centre and never exceeds the cap...\n");
    const double aspect = 16.0 / 9.0;
    const ViewRect base = FitVideoRect(1920, 1080, aspect);

    const ViewRect z2 = FitVideoRect(1920, 1080, aspect, ViewTransform{2.0, 0, 0});
    Check(Near(z2.width, base.width * 2) && Near(z2.height, base.height * 2), "twice the size");
    Check(Near(z2.midX(), base.midX()) && Near(z2.midY(), base.midY()),
        "and still centred on the same point");

    const ViewTransform beyond =
        ApplyGesture(ViewTransform{1.0, 0, 0}, 100.0, 960, 540, 0, 0, 1920, 1080, aspect);
    Check(Near(beyond.zoom, kMaxViewZoom), "a wild pinch is capped, not obeyed");

    const ViewTransform under =
        ApplyGesture(ViewTransform{1.0, 0, 0}, 0.01, 960, 540, 0, 0, 1920, 1080, aspect);
    Check(Near(under.zoom, 1.0), "and it never zooms out past fit-to-window");
}

void TestPanCannotOpenAGap() {
    std::printf("[viewfit] panning stops at the video edge, so no background shows through...\n");
    const double aspect = 16.0 / 9.0;

    const ViewRect nudged =
        FitVideoRect(1920, 1080, aspect, ViewTransform{1.0, 500, 500});
    const ViewRect centred = FitVideoRect(1920, 1080, aspect);
    Check(Near(nudged.x, centred.x) && Near(nudged.y, centred.y),
        "at fit-to-window there is nowhere to pan, so pan is ignored");

    const ViewRect dragged =
        FitVideoRect(1920, 1080, aspect, ViewTransform{2.0, 100'000, 100'000});
    Check(dragged.x <= 0.001 && dragged.y <= 0.001, "the left/top edge never comes inside");
    Check(dragged.x + dragged.width >= 1920 - 0.001 &&
              dragged.y + dragged.height >= 1080 - 0.001,
        "and neither does the right/bottom edge");
}

void TestGestureKeepsTheFocalPoint() {
    std::printf("[viewfit] pinching about a corner keeps that corner under the fingers...\n");
    const double aspect = 16.0 / 9.0;
    const ViewTransform start{1.0, 0, 0};

    const ViewTransform t = ApplyGesture(start, 2.0, 0, 0, 0, 0, 1920, 1080, aspect);
    Check(Near(t.zoom, 2.0), "zoom applied");
    const ViewRect r = FitVideoRect(1920, 1080, aspect, t);
    Check(Near(r.x, 0) && Near(r.y, 0),
        "zooming into the top-left pins the video's top-left to the viewport corner");

    const ViewTransform pan = ApplyGesture(t, 1.0, 960, 540, -50, -50, 1920, 1080, aspect);
    Check(Near(pan.zoom, 2.0), "a pure drag does not change zoom");
    Check(pan.panX < t.panX && pan.panY < t.panY, "and it moves the content");
}

void TestNormalizeAxisSpansTheFullRange() {
    std::printf("[viewfit] the first and last pixel map to exactly 0 and 65535...\n");
    Check(NormalizeAxis(0, 1920) == 0, "left edge is 0");
    Check(NormalizeAxis(1919, 1920) == kNormalizedMax, "last pixel is exactly 65535");
    Check(NormalizeAxis(959.5, 1920) == kNormalizedMax / 2, "the middle lands in the middle");

    Check(NormalizeAxis(-100, 1920) == 0, "a point left of the edge clamps to 0");
    Check(NormalizeAxis(99'999, 1920) == kNormalizedMax, "and past the right edge to 65535");

    Check(NormalizeAxis(5, 1) == 0, "a one-pixel extent has no range to speak of");
    Check(NormalizeAxis(5, 0) == 0, "nor does a zero extent");
}

void TestNormalizePointerRejectsOutsideTheVideo() {
    std::printf("[viewfit] a click in the letterbox bars is not a click on the desktop...\n");
    const ViewRect r{100, 50, 800, 450};
    int32_t nx = -1, ny = -1;

    Check(NormalizePointer(100, 50, r, nx, ny) && nx == 0 && ny == 0, "the top-left corner");
    Check(NormalizePointer(899, 499, r, nx, ny) && nx == kNormalizedMax && ny == kNormalizedMax,
        "the bottom-right pixel");

    nx = ny = -1;
    Check(!NormalizePointer(99, 200, r, nx, ny), "just left of the video is rejected");
    Check(!NormalizePointer(900, 200, r, nx, ny), "the right edge is exclusive");
    Check(!NormalizePointer(200, 49, r, nx, ny), "above the video is rejected");
    Check(!NormalizePointer(200, 500, r, nx, ny), "the bottom edge is exclusive");
    Check(nx == -1 && ny == -1, "a rejected point never writes to the outputs");

    const ViewRect empty{0, 0, 0, 0};
    Check(!NormalizePointer(0, 0, empty, nx, ny), "an empty rect maps nothing");
}

void TestPointerRoundTripAcrossTheWholeVideo() {
    std::printf("[viewfit] every point inside the video maps into range, monotonically...\n");
    const double aspect = 16.0 / 9.0;
    const ViewRect r = FitVideoRect(1600, 1200, aspect, ViewTransform{1.5, 40, -25});

    bool inRange = true, monotonic = true;
    int32_t prev = -1;
    for (int i = 0; i < 400; ++i) {
        const double px = r.x + r.width * i / 400.0;
        int32_t nx = 0, ny = 0;
        if (!NormalizePointer(px, r.y + r.height / 2, r, nx, ny)) {
            inRange = false;
            break;
        }
        if (nx < 0 || nx > kNormalizedMax) inRange = false;
        if (nx < prev) monotonic = false;
        prev = nx;
    }
    Check(inRange, "no sample fell outside 0..65535");
    Check(monotonic, "and moving right never moves the remote cursor left");
}

void TestScaleToFitNeverUpscales() {
    std::printf("[viewfit] a window is shrunk to fit the work area but never blown up...\n");

    Check(ScaleToFit(1920, 1080, 3840, 2160) == ViewSize{1920, 1080},
        "a video smaller than the work area keeps its native size");

    const ViewSize shrunk = ScaleToFit(3840, 2160, 1920, 1200);
    Check(shrunk == ViewSize{1920, 1080}, "a 4K video is limited by width, aspect preserved");

    const ViewSize byHeight = ScaleToFit(1000, 1000, 1920, 500);
    Check(byHeight == ViewSize{500, 500}, "a square video is limited by height instead");

    Check(ScaleToFit(0, 1080, 1920, 1080) == ViewSize{}, "an unknown size yields nothing");
    Check(ScaleToFit(1920, 1080, 0, 0) == ViewSize{1920, 1080},
        "an unknown work area leaves the size alone");

    const ViewSize tiny = ScaleToFit(4000, 4, 100, 100);
    Check(tiny.width >= 1 && tiny.height >= 1, "extreme aspect never collapses to zero");
}

}

namespace {

void TestZoomedThreshold() {
    std::printf("[viewfit] the zoomed flag ignores rounding jitter around 1.0...\n");
    Check(!IsZoomed(1.0), "exactly 1.0 is not zoomed");
    Check(!IsZoomed(1.005), "a hair over 1.0 is still just fit");
    Check(IsZoomed(1.02), "a real zoom is zoomed");
    Check(!IsZoomed(0.5), "scaled down to fit is not zoomed");
}

}

void RunViewFitTests() {
    TestLetterboxing();
    TestZoomedThreshold();
    TestDegenerateInputs();
    TestZoomIsCentredAndBounded();
    TestPanCannotOpenAGap();
    TestGestureKeepsTheFocalPoint();
    TestNormalizeAxisSpansTheFullRange();
    TestNormalizePointerRejectsOutsideTheVideo();
    TestPointerRoundTripAcrossTheWholeVideo();
    TestScaleToFitNeverUpscales();
}
