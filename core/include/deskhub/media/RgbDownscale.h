#pragma once
#include <cstdint>
#include <vector>

namespace deskhub::media {

inline constexpr uint32_t kPackedPixelBytes = 4;
inline constexpr uint32_t kOpaqueHighByte = 0xFF000000u;

class RgbDownscaler {
public:
    void Configure(uint32_t srcWidth, uint32_t srcHeight, uint32_t dstWidth, uint32_t dstHeight);

    bool Matches(uint32_t srcWidth, uint32_t srcHeight, uint32_t dstWidth,
        uint32_t dstHeight) const {
        return srcW_ == srcWidth && srcH_ == srcHeight && dstW_ == dstWidth &&
               dstH_ == dstHeight;
    }

    bool ready() const {
        return dstW_ != 0 && dstH_ != 0;
    }

    void Scale(const uint8_t* src, uint32_t srcStride, uint8_t* dst, uint32_t dstStride) const;

private:
    std::vector<uint32_t> xStart_;
    std::vector<uint32_t> yStart_;
    uint32_t srcW_ = 0, srcH_ = 0, dstW_ = 0, dstH_ = 0;
};

}
