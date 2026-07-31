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

inline constexpr uint32_t kMinEncodeWidth = 160;
inline constexpr uint32_t kMinEncodeHeight = 64;

struct EncodeSize {
    StreamSize size;
    bool tooSmall = false;

    uint32_t width() const {
        return size.width;
    }
    uint32_t height() const {
        return size.height;
    }
};

StreamSize FitStreamSize(uint32_t srcW, uint32_t srcH, uint32_t maxDim, uint32_t clientW,
    uint32_t clientH);

StreamSize ApplyQualityScale(StreamSize base, uint32_t scalePct);

EncodeSize ClampEncodeSize(uint32_t nativeW, uint32_t nativeH, uint32_t wantW, uint32_t wantH,
    uint32_t maxDim);

}
