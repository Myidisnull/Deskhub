#pragma once
#include <cstdint>

namespace deskhub::media {

struct AlignedEncodeSize {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mbWidth = 0;
    uint32_t mbHeight = 0;
    uint32_t cropRightOffset = 0;
    uint32_t cropBottomOffset = 0;
    bool cropped = false;
};

inline constexpr uint32_t kH264MacroblockPx = 16;

AlignedEncodeSize AlignEncodeSize(uint32_t width, uint32_t height, uint32_t align);

uint8_t LevelFor(uint32_t mbWidth, uint32_t mbHeight, uint32_t fps);

}
