#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/media/H264Encode.h"

#include <cstdio>

using namespace deskhub::media;

namespace {

void TestAnAlreadyAlignedSizeIsLeftAlone() {
    std::printf("[h264] a size that is already a whole number of macroblocks is untouched...\n");
    const AlignedEncodeSize a = AlignEncodeSize(1920, 1088, kH264MacroblockPx);
    Check(a.width == 1920 && a.height == 1088, "the size comes back as it went in");
    Check(a.mbWidth == 120 && a.mbHeight == 68, "and is reported in macroblocks");
    Check(!a.cropped, "nothing needs cropping");
    Check(a.cropRightOffset == 0 && a.cropBottomOffset == 0, "so both offsets are zero");
}

void TestPaddingIsReportedAsSpsCropOffsets() {
    std::printf("[h264] padding to the next macroblock is reported as SPS crop offsets...\n");
    const AlignedEncodeSize a = AlignEncodeSize(1920, 1080, kH264MacroblockPx);
    Check(a.width == 1920 && a.height == 1088, "1080 rounds up to 1088");
    Check(a.mbWidth == 120 && a.mbHeight == 68, "68 macroblock rows");
    Check(a.cropped, "the frame is cropped back down for the decoder");
    Check(a.cropRightOffset == 0, "the width needed no padding");
    Check(a.cropBottomOffset == 4, "and the 8 padded lines are 4 crop units");
}

void TestAZeroSizeAlignsToNothing() {
    std::printf("[h264] a zero size or a zero alignment yields nothing to encode...\n");
    Check(AlignEncodeSize(0, 1080, kH264MacroblockPx).width == 0, "a zero width is refused");
    Check(AlignEncodeSize(1920, 0, kH264MacroblockPx).height == 0, "so is a zero height");
    Check(AlignEncodeSize(1920, 1080, 0).width == 0, "and a zero alignment cannot divide");
}

void TestLevelFollowsTheMacroblockRate() {
    std::printf("[h264] the level is the smallest one that fits the macroblock rate...\n");
    const AlignedEncodeSize hd = AlignEncodeSize(1280, 720, kH264MacroblockPx);
    Check(LevelFor(hd.mbWidth, hd.mbHeight, 30) == 31, "720p30 fits level 3.1");
    Check(LevelFor(hd.mbWidth, hd.mbHeight, 60) == 32, "720p60 needs 3.2");

    const AlignedEncodeSize fhd = AlignEncodeSize(1920, 1080, kH264MacroblockPx);
    Check(LevelFor(fhd.mbWidth, fhd.mbHeight, 30) == 40, "1080p30 fits level 4.0");
    Check(LevelFor(fhd.mbWidth, fhd.mbHeight, 60) == 42, "1080p60 needs 4.2");

    const AlignedEncodeSize uhd = AlignEncodeSize(3840, 2160, kH264MacroblockPx);
    Check(LevelFor(uhd.mbWidth, uhd.mbHeight, 60) == 52, "2160p60 needs 5.2");
}

void TestLevelSaturatesRatherThanWrapping() {
    std::printf("[h264] a rate past the last level saturates at the highest one...\n");
    Check(LevelFor(1000, 1000, 240) == 62, "there is nothing above 6.2 to pick");
    Check(LevelFor(120, 68, 0) == 42, "a zero fps is read as the 60 the encoder defaults to");
}

}

void RunH264EncodeTests() {
    TestAnAlreadyAlignedSizeIsLeftAlone();
    TestPaddingIsReportedAsSpsCropOffsets();
    TestAZeroSizeAlignsToNothing();
    TestLevelFollowsTheMacroblockRate();
    TestLevelSaturatesRatherThanWrapping();
}
