#include "deskhub/media/RgbDownscale.h"

#include <cstring>

namespace deskhub::media {
namespace {

void BuildEdges(std::vector<uint32_t>& edges, uint32_t srcExtent, uint32_t dstExtent) {
    edges.resize(dstExtent + 1);
    for (uint32_t i = 0; i <= dstExtent; ++i)
        edges[i] = uint32_t(uint64_t(i) * srcExtent / dstExtent);
}

uint32_t LoadPixel(const uint8_t* at) {
    uint32_t pixel = 0;
    std::memcpy(&pixel, at, sizeof(pixel));
    return pixel;
}

uint32_t AveragePixels(const uint8_t* src, uint32_t srcStride, uint32_t x0, uint32_t x1,
    uint32_t y0, uint32_t y1) {
    uint32_t low = 0, mid = 0, high = 0;
    for (uint32_t y = y0; y < y1; ++y) {
        const uint8_t* row = src + size_t(y) * srcStride + size_t(x0) * kPackedPixelBytes;
        for (uint32_t x = x0; x < x1; ++x, row += kPackedPixelBytes) {
            const uint32_t pixel = LoadPixel(row);
            low += pixel & 0xFFu;
            mid += pixel >> 8 & 0xFFu;
            high += pixel >> 16 & 0xFFu;
        }
    }
    const uint32_t count = (x1 - x0) * (y1 - y0);
    return low / count | (mid / count) << 8 | (high / count) << 16 | kOpaqueHighByte;
}

}

void RgbDownscaler::Configure(uint32_t srcWidth, uint32_t srcHeight, uint32_t dstWidth,
    uint32_t dstHeight) {
    srcW_ = srcWidth;
    srcH_ = srcHeight;
    dstW_ = dstWidth > srcWidth ? srcWidth : dstWidth;
    dstH_ = dstHeight > srcHeight ? srcHeight : dstHeight;
    if (!ready()) return;
    BuildEdges(xStart_, srcW_, dstW_);
    BuildEdges(yStart_, srcH_, dstH_);
}

void RgbDownscaler::Scale(const uint8_t* src, uint32_t srcStride, uint8_t* dst,
    uint32_t dstStride) const {
    if (!ready() || !src || !dst) return;

    for (uint32_t y = 0; y < dstH_; ++y) {
        const uint32_t y0 = yStart_[y];
        const uint32_t y1 = yStart_[y + 1] > y0 ? yStart_[y + 1] : y0 + 1;
        uint8_t* out = dst + size_t(y) * dstStride;
        for (uint32_t x = 0; x < dstW_; ++x, out += kPackedPixelBytes) {
            const uint32_t x0 = xStart_[x];
            const uint32_t x1 = xStart_[x + 1] > x0 ? xStart_[x + 1] : x0 + 1;
            const uint32_t pixel = AveragePixels(src, srcStride, x0, x1, y0, y1);
            std::memcpy(out, &pixel, sizeof(pixel));
        }
    }
}

}
