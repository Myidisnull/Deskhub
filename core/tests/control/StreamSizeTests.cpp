#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/control/StreamSize.h"

#include <cmath>
#include <cstdio>

using namespace deskhub;

namespace {

double AspectDriftPct(uint32_t srcW, uint32_t srcH, StreamSize s) {
    const double a0 = double(srcW) / double(srcH), a1 = double(s.width) / double(s.height);
    return std::fabs(a1 - a0) / a0 * 100.0;
}

void CheckEvenAndAligned(uint32_t srcW, uint32_t srcH, StreamSize s, const char* what) {
    Check((s.width & 1u) == 0 && (s.height & 1u) == 0, what);
    Check(AspectDriftPct(srcW, srcH, s) < 0.2, "aspect ratio preserved (<0.2% drift)");
}

void TestUserCap() {
    std::printf("[size] user cap shrinks the long edge, aspect preserved...\n");
    const StreamSize s = FitStreamSize(3024, 1964, 1920, 0, 0);
    Check(s.width == 1920, "long edge lands exactly on the cap");
    Check(s == StreamSize{1920, 1246}, "3024x1964 cap 1920 -> 1920x1246");
    CheckEvenAndAligned(3024, 1964, s, "capped size is even on both axes");

    const StreamSize x = FitStreamSize(6016, 3384, 1920, 0, 0);
    Check(x == StreamSize{1920, 1080}, "6016x3384 cap 1920 -> exactly 1920x1080");
}

void TestNoUpscale() {
    std::printf("[size] never upscales: source under every cap is passed through...\n");
    Check(FitStreamSize(1280, 720, 1920, 0, 0) == StreamSize{1280, 720},
        "720p source, 1920 cap -> untouched");
    Check(FitStreamSize(1280, 720, 1920, 3840, 2160) == StreamSize{1280, 720},
        "a big client does not make the host invent pixels");
    Check(FitStreamSize(1920, 1080, 0, 0, 0) == StreamSize{1920, 1080},
        "no caps at all -> native");
}

void TestPortraitSource() {
    std::printf("[size] portrait source caps its own long edge (height)...\n");
    const StreamSize s = FitStreamSize(2160, 3840, 1920, 0, 0);
    Check(s == StreamSize{1080, 1920}, "2160x3840 cap 1920 -> 1080x1920, height is the long edge");
    CheckEvenAndAligned(2160, 3840, s, "portrait result is even");
}

void TestClientCapPhone() {
    std::printf("[size] a phone's screen caps the stream below the user cap...\n");
    const StreamSize s = FitStreamSize(3024, 1964, 1920, 1179, 2556);
    Check(s.width < 1920, "client screen is the binding limit, not the 1920 user cap");
    Check(s == StreamSize{1814, 1178}, "3024x1964 into a 2556x1179 landscape rect");
    CheckEvenAndAligned(3024, 1964, s, "client-capped size is even");

    Check(FitStreamSize(3024, 1964, 1920, 2556, 1179) == s,
        "orientation the client reports does not change the answer");

    const StreamSize small = FitStreamSize(3024, 1964, 1920, 750, 1334);
    Check(small == StreamSize{1154, 750}, "750x1334 phone -> 1154x750");
    CheckEvenAndAligned(3024, 1964, small, "small-phone result is even");
    Check(small.width * small.height < 1'000'000, "under 1 Mpixel");
}

void TestClientCapTabletLosesToUserCap() {
    std::printf("[size] a big tablet loses to the user cap (tighter of the two wins)...\n");
    Check(FitStreamSize(3024, 1964, 1920, 2048, 2732) == StreamSize{1920, 1246},
        "tablet bigger than the cap -> cap decides");
    const StreamSize s = FitStreamSize(3024, 1964, 0, 2048, 2732);
    Check(s.width > 1920 && s.width <= 2732, "no user cap -> the tablet's own screen bounds it");
}

void TestLegacyClientAndGarbage() {
    std::printf("[size] legacy 3840x2160 clients and nonsense sizes fall back safely...\n");
    Check(FitStreamSize(3024, 1964, 1920, 3840, 2160) == FitStreamSize(3024, 1964, 1920, 0, 0),
        "a legacy client changes nothing");
    Check(FitStreamSize(0, 0, 1920, 0, 0) == StreamSize{}, "empty source -> empty result");
    Check(FitStreamSize(3024, 1964, 1, 0, 0) == StreamSize{3024, 1964},
        "absurd cap falls back to native instead of returning an unencodable size");
    Check(FitStreamSize(3024, 1964, 0, 1, 1) == StreamSize{3024, 1964},
        "absurd client size falls back to native");
}

}

void RunStreamSizeTests() {
    TestUserCap();
    TestNoUpscale();
    TestPortraitSource();
    TestClientCapPhone();
    TestClientCapTabletLosesToUserCap();
    TestLegacyClientAndGarbage();
}
