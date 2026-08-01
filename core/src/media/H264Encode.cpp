#include "deskhub/media/H264Encode.h"

namespace deskhub::media {

namespace {

constexpr uint32_t kCropOffsetUnitPx = 2;

struct LevelLimits {
    uint8_t idc;
    uint64_t maxMbps;
    uint64_t maxFrameMbs;
};

constexpr LevelLimits kLevels[] = {
    {30, 40500, 1620},
    {31, 108000, 3600},
    {32, 216000, 5120},
    {40, 245760, 8192},
    {42, 522240, 8704},
    {50, 589824, 22080},
    {51, 983040, 36864},
    {52, 2073600, 36864},
    {60, 4177920, 139264},
    {61, 8355840, 139264},
    {62, 16711680, 139264},
};

constexpr uint32_t kDefaultFps = 60;

}

AlignedEncodeSize AlignEncodeSize(uint32_t width, uint32_t height, uint32_t align) {
    AlignedEncodeSize out;
    if (!width || !height || !align) return out;

    out.mbWidth = (width + align - 1) / align;
    out.mbHeight = (height + align - 1) / align;
    out.width = out.mbWidth * align;
    out.height = out.mbHeight * align;
    out.cropRightOffset = (out.width - width) / kCropOffsetUnitPx;
    out.cropBottomOffset = (out.height - height) / kCropOffsetUnitPx;
    out.cropped = out.width != width || out.height != height;
    return out;
}

uint8_t LevelFor(uint32_t mbWidth, uint32_t mbHeight, uint32_t fps) {
    const uint64_t frameMbs = uint64_t(mbWidth) * mbHeight;
    const uint64_t mbps = frameMbs * (fps ? fps : kDefaultFps);
    for (const LevelLimits& l : kLevels)
        if (mbps <= l.maxMbps && frameMbs <= l.maxFrameMbs) return l.idc;
    return kLevels[sizeof(kLevels) / sizeof(kLevels[0]) - 1].idc;
}

}
