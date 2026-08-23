#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/control/StreamSize.h"
#include "deskhub/session/host/SourcePipeline.h"

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

void TestQualityScale() {
    std::printf("[size] a quality rung shrinks the fitted size and keeps it even...\n");
    Check(ApplyQualityScale({1920, 1080}, 100) == StreamSize{1920, 1080},
        "the top rung changes nothing");
    Check(ApplyQualityScale({1920, 1080}, 0) == StreamSize{1920, 1080},
        "an unset percentage means full size, not zero");
    Check(ApplyQualityScale({1920, 1080}, 150) == StreamSize{1920, 1080},
        "a percentage over 100 never upscales");

    const StreamSize s = ApplyQualityScale({1920, 1080}, 75);
    Check(s == StreamSize{1440, 810}, "75% of 1920x1080");
    Check((s.width & 1u) == 0 && (s.height & 1u) == 0, "both edges stay even");

    Check(ApplyQualityScale({1918, 1078}, 75) == StreamSize{1438, 808},
        "odd results are rounded down to even, never up");
    Check(ApplyQualityScale({2, 2}, 25) == StreamSize{0, 0},
        "scaling below one pixel yields an empty size for the caller to reject");
}

void TestTargetStreamSize() {
    std::printf("[size] one formula turns a source into the size a host should stream...\n");
    Check(TargetStreamSize(3840, 2160, 1920, 0, 0, 100) == StreamSize{1920, 1080},
        "the cap alone bounds the source");
    Check(TargetStreamSize(3840, 2160, 1920, 1280, 720, 100) == StreamSize{1280, 720},
        "a smaller client wins over the cap");
    Check(TargetStreamSize(3840, 2160, 1920, 1280, 720, 50) == StreamSize{640, 360},
        "the quality rung applies on top of the fit");
    Check(TargetStreamSize(0, 0, 1920, 0, 0, 100) == StreamSize{},
        "no source means nothing to stream");
    Check(TargetStreamSize(2, 2, 0, 0, 0, 25) == StreamSize{},
        "a rung that scales below one pixel reports empty rather than a bad size");

    Check(TargetStreamSize(3840, 2160, 1920, 1280, 720, 50) == ApplyQualityScale(FitStreamSize(3840, 2160, 1920, 1280, 720), 50),
        "it stays the composition of the two steps it replaces");
}

void TestEvenDown() {
    std::printf("[size] encoders want even edges, so sizes always round down...\n");
    Check(EvenDown(1.9) == 0, "anything under two pixels is not a size at all");
    Check(EvenDown(2.0) == 2, "two is the smallest even size");
    Check(EvenDown(1921.6) == 1920, "an odd result rounds down, never up");
    Check(EvenDown(1920.0) == 1920, "an already-even size is left alone");
}

void TestRetargetStream() {
    std::printf("[size] retargeting a source publishes the size the encoder should use...\n");
    SourcePipelineState st(20'000'000, 1'000'000);

    Check(RetargetStream(st, 1920) == StreamSize{}, "no captured frame yet -> nothing to target");
    Check(st.wantW.load() == 0 && st.wantH.load() == 0, "and nothing is published");

    st.nativeW.store(3840);
    st.nativeH.store(2160);
    Check(RetargetStream(st, 1920) == StreamSize{1920, 1080}, "the user cap bounds the source");
    Check(st.wantW.load() == 1920 && st.wantH.load() == 1080, "the result is published");

    st.cliW = 1280;
    st.cliH = 720;
    Check(RetargetStream(st, 1920) == StreamSize{1280, 720}, "a smaller client wins over the cap");

    st.step.scalePct = 50;
    Check(RetargetStream(st, 1920) == StreamSize{640, 360}, "the quality rung applies on top");
    Check(st.wantW.load() == 640 && st.wantH.load() == 360, "the scaled result is published");

    st.nativeW.store(0);
    Check(RetargetStream(st, 1920) == StreamSize{}, "losing the source reports empty");
    Check(st.wantW.load() == 640, "but the last good size is left alone for the encoder");
}

void TestAdmitCapturedFrame() {
    std::printf("[size] admitting a captured frame drives resize, pause and resume...\n");
    SourcePipelineState st(20'000'000, 1'000'000);

    const FrameAdmission empty = AdmitCapturedFrame(st, 0, 0, 1920);
    Check(empty.drop && !empty.rebuildEncoder, "an empty frame is dropped outright");
    Check(st.nativeW.load() == 0, "and leaves no trace");

    const FrameAdmission first = AdmitCapturedFrame(st, 3840, 2160, 1920);
    Check(!first.drop, "the first real frame is admitted");
    Check(first.rebuildEncoder, "and asks for an encoder");
    Check(first.encode == StreamSize{1920, 1080}, "at the capped size");
    Check(first.sizeNote.empty(), "with no resize story to tell yet");
    Check(st.srcW.load() == 1920 && st.srcH.load() == 1080, "the encode size is published");
    Check(st.nativeW.load() == 3840 && st.nativeH.load() == 2160, "so is the native size");
    Check(st.sizeChanged.exchange(false), "and the offer is marked stale");

    const FrameAdmission same = AdmitCapturedFrame(st, 3840, 2160, 1920);
    Check(!same.drop && !same.rebuildEncoder, "a steady-state frame changes nothing");
    Check(same.sizeNote.empty() && same.pauseNote.empty(), "and says nothing");

    const FrameAdmission resized = AdmitCapturedFrame(st, 1280, 720, 1920);
    Check(resized.rebuildEncoder && !resized.drop, "a resize rebuilds the encoder");
    Check(!resized.sizeNote.empty(), "and explains itself");
    Check(st.sizeChanged.exchange(false), "and marks the offer stale again");

    const FrameAdmission tiny = AdmitCapturedFrame(st, 120, 40, 1920);
    Check(tiny.drop, "a source below the floor is not encoded");
    Check(!tiny.pauseNote.empty(), "the first tiny frame announces the pause");
    Check(st.paused.load(), "and the source is paused");
    Check(AdmitCapturedFrame(st, 120, 40, 1920).pauseNote.empty(),
        "later tiny frames stay quiet");

    const FrameAdmission grown = AdmitCapturedFrame(st, 1280, 720, 1920);
    Check(!grown.drop, "a grown-back source is admitted");
    Check(!grown.pauseNote.empty(), "announces the resume");
    Check(!st.paused.load(), "and clears the pause");
}

void TestAdmitRotatedFrame() {
    std::printf("[size] a rotated source retargets before it is clamped...\n");
    SourcePipelineState st(20'000'000, 1'000'000);

    const FrameAdmission portrait = AdmitCapturedFrame(st, 1170, 2532, 1920);
    Check(!portrait.drop && portrait.rebuildEncoder, "the portrait stream starts");
    Check(st.srcH.load() > st.srcW.load(), "and encodes taller than wide");
    st.sizeChanged.store(false);

    const FrameAdmission landscape = AdmitCapturedFrame(st, 2532, 1170, 1920);
    Check(landscape.rebuildEncoder && !landscape.drop, "rotating rebuilds the encoder");
    Check(st.srcW.load() > st.srcH.load(), "the encode turns wider than tall");
    const double sourceAspect = 2532.0 / 1170.0;
    const double encodeAspect = double(st.srcW.load()) / double(st.srcH.load());
    Check(encodeAspect > sourceAspect * 0.98 && encodeAspect < sourceAspect * 1.02,
        "the encode keeps the rotated aspect instead of clamping to the old target");
    Check(st.sizeChanged.load(), "and the offer is marked stale for the viewers");
}

void TestMakeEncoderConfig() {
    std::printf("[size] the encoder config is derived from the pipeline state...\n");
    SourcePipelineState st(20'000'000, 1'000'000);

    const media::EncoderConfig fresh = MakeEncoderConfig(st, {1920, 1080}, 60);
    Check(fresh.width == 1920 && fresh.height == 1080, "the encode size is taken as given");
    Check(fresh.fps == 60, "no negotiated fps yet -> the option fps");
    Check(fresh.bitrateBps == 20'000'000, "the start bitrate carries over");

    st.curFps.store(30);
    st.curBitrateBps.store(5'000'000);
    const media::EncoderConfig tuned = MakeEncoderConfig(st, {1280, 720}, 60);
    Check(tuned.fps == 30, "a negotiated fps wins over the option");
    Check(tuned.bitrateBps == 5'000'000, "so does the controlled bitrate");
}

void TestClampEncodeSize() {
    std::printf("[size] the encoder size is clamped to the source, even, and floored...\n");
    Check(ClampEncodeSize(0, 0, 1920, 1080, 1920).size == StreamSize{},
        "no captured frame -> empty");

    const EncodeSize fromCap = ClampEncodeSize(3840, 2160, 0, 0, 1920);
    Check(fromCap.size == StreamSize{1920, 1080},
        "no negotiated size yet -> the user cap decides");
    Check(!fromCap.tooSmall, "1920x1080 is encodable");

    Check(ClampEncodeSize(1280, 720, 1920, 1080, 1920).size == StreamSize{1280, 720},
        "a negotiated size larger than the source is clamped to the source");

    Check(ClampEncodeSize(1281, 721, 1281, 721, 0).size == StreamSize{1280, 720},
        "odd edges are rounded down to even");

    const EncodeSize tiny = ClampEncodeSize(320, 200, 100, 40, 0);
    Check(tiny.size == StreamSize{100, 40}, "the requested size is honoured");
    Check(tiny.tooSmall, "but it is flagged as below the encoder floor");

    Check(!ClampEncodeSize(320, 200, kMinEncodeWidth, kMinEncodeHeight, 0).tooSmall,
        "exactly on the floor is still encodable");
    Check(ClampEncodeSize(320, 200, kMinEncodeWidth - 2, kMinEncodeHeight, 0).tooSmall,
        "one even step under the width floor is not");
}

}

void RunStreamSizeTests() {
    TestUserCap();
    TestNoUpscale();
    TestPortraitSource();
    TestClientCapPhone();
    TestClientCapTabletLosesToUserCap();
    TestLegacyClientAndGarbage();
    TestQualityScale();
    TestEvenDown();
    TestTargetStreamSize();
    TestRetargetStream();
    TestAdmitCapturedFrame();
    TestAdmitRotatedFrame();
    TestMakeEncoderConfig();
    TestClampEncodeSize();
}
