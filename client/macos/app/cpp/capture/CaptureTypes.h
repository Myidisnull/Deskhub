#pragma once
#include <cstdint>

struct MacFrameInfo {
    void* pixelBuffer;
    uint32_t width;
    uint32_t height;
    uint64_t timestampUs;
};
