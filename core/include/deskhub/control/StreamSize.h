#pragma once
#include <cstdint>

namespace deskhub {

struct StreamSize {
    uint32_t width = 0;
    uint32_t height = 0;

    friend bool operator==(const StreamSize& a, const StreamSize& b) {
        return a.width == b.width && a.height == b.height;
    }
};

StreamSize FitStreamSize(uint32_t srcW, uint32_t srcH, uint32_t maxDim, uint32_t clientW,
    uint32_t clientH);

}
