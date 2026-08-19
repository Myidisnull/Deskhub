#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/media/RgbDownscale.h"

#include <cstdio>
#include <cstring>
#include <vector>

using deskhub::media::kPackedPixelBytes;
using deskhub::media::RgbDownscaler;

namespace {

std::vector<uint8_t> Canvas(uint32_t height, uint32_t stride) {
    return std::vector<uint8_t>(size_t(stride) * height, 0);
}

void PutPixel(std::vector<uint8_t>& buf, uint32_t stride, uint32_t x, uint32_t y, uint8_t low,
    uint8_t mid, uint8_t high) {
    uint8_t* p = buf.data() + size_t(y) * stride + size_t(x) * kPackedPixelBytes;
    p[0] = low;
    p[1] = mid;
    p[2] = high;
    p[3] = 0;
}

const uint8_t* PixelAt(const std::vector<uint8_t>& buf, uint32_t stride, uint32_t x, uint32_t y) {
    return buf.data() + size_t(y) * stride + size_t(x) * kPackedPixelBytes;
}

void TestFourPixelsAverageIntoOne() {
    std::printf("[downscale] a 2x2 block averages into one pixel...\n");
    const uint32_t srcStride = 2 * kPackedPixelBytes;
    std::vector<uint8_t> src = Canvas(2, srcStride);
    PutPixel(src, srcStride, 0, 0, 0, 10, 100);
    PutPixel(src, srcStride, 1, 0, 2, 20, 100);
    PutPixel(src, srcStride, 0, 1, 4, 30, 100);
    PutPixel(src, srcStride, 1, 1, 6, 40, 100);

    RgbDownscaler scaler;
    scaler.Configure(2, 2, 1, 1);
    std::vector<uint8_t> dst = Canvas(1, kPackedPixelBytes);
    scaler.Scale(src.data(), srcStride, dst.data(), kPackedPixelBytes);

    const uint8_t* p = dst.data();
    Check(p[0] == 3, "the first channel is the mean of 0, 2, 4, 6");
    Check(p[1] == 25, "the second channel is the mean of 10, 20, 30, 40");
    Check(p[2] == 100, "a flat channel survives averaging unchanged");
    Check(p[3] == 0xFF, "the unused high byte is written opaque, not left as garbage");
}

void TestSameSizeIsAFaithfulCopy() {
    std::printf("[downscale] asking for the source size copies every pixel through...\n");
    const uint32_t w = 5, h = 3, stride = w * kPackedPixelBytes;
    std::vector<uint8_t> src = Canvas(h, stride);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
            PutPixel(src, stride, x, y, uint8_t(x), uint8_t(y), uint8_t(x * y));

    RgbDownscaler scaler;
    scaler.Configure(w, h, w, h);
    std::vector<uint8_t> dst = Canvas(h, stride);
    scaler.Scale(src.data(), stride, dst.data(), stride);

    bool identical = true;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t* s = PixelAt(src, stride, x, y);
            const uint8_t* d = PixelAt(dst, stride, x, y);
            if (s[0] != d[0] || s[1] != d[1] || s[2] != d[2]) identical = false;
        }
    Check(identical, "every colour channel arrives unchanged");
}

void TestFlatFieldStaysFlat() {
    std::printf("[downscale] a flat colour downscales to the same flat colour...\n");
    const uint32_t sw = 3440, sh = 1440, srcStride = sw * kPackedPixelBytes;
    std::vector<uint8_t> src(size_t(srcStride) * sh);
    for (size_t i = 0; i < src.size(); i += kPackedPixelBytes) {
        src[i] = 17;
        src[i + 1] = 99;
        src[i + 2] = 200;
        src[i + 3] = 0;
    }

    const uint32_t dw = 1280, dh = 534, dstStride = dw * kPackedPixelBytes;
    RgbDownscaler scaler;
    scaler.Configure(sw, sh, dw, dh);
    std::vector<uint8_t> dst(size_t(dstStride) * dh, 0);
    scaler.Scale(src.data(), srcStride, dst.data(), dstStride);

    bool flat = true;
    for (uint32_t y = 0; y < dh; ++y)
        for (uint32_t x = 0; x < dw; ++x) {
            const uint8_t* p = PixelAt(dst, dstStride, x, y);
            if (p[0] != 17 || p[1] != 99 || p[2] != 200 || p[3] != 0xFF) flat = false;
        }
    Check(flat, "no edge pixel is left short of its source samples");
}

void TestNoSampleFallsOutsideTheSource() {
    std::printf("[downscale] an awkward ratio never reads past the last row or column...\n");
    const uint32_t sw = 101, sh = 37, srcStride = sw * kPackedPixelBytes;
    std::vector<uint8_t> src(size_t(srcStride) * sh, 0x5A);
    const uint32_t dw = 40, dh = 9, dstStride = dw * kPackedPixelBytes;

    RgbDownscaler scaler;
    scaler.Configure(sw, sh, dw, dh);
    std::vector<uint8_t> dst(size_t(dstStride) * dh, 0);
    scaler.Scale(src.data(), srcStride, dst.data(), dstStride);

    bool uniform = true;
    for (uint32_t y = 0; y < dh; ++y)
        for (uint32_t x = 0; x < dw; ++x)
            if (PixelAt(dst, dstStride, x, y)[0] != 0x5A) uniform = false;
    Check(uniform, "every destination pixel averaged only real source pixels");
}

void TestUpscaleIsRefused() {
    std::printf("[downscale] a request larger than the source is clamped, not invented...\n");
    const uint32_t sw = 4, sh = 4, stride = sw * kPackedPixelBytes;
    std::vector<uint8_t> src(size_t(stride) * sh, 0x30);

    RgbDownscaler scaler;
    scaler.Configure(sw, sh, 64, 64);
    Check(scaler.Matches(sw, sh, sw, sh), "the target is clamped to the source size");

    std::vector<uint8_t> dst(size_t(stride) * sh, 0);
    scaler.Scale(src.data(), stride, dst.data(), stride);
    Check(dst[0] == 0x30, "and it still scales the frame it was given");
}

void TestUnconfiguredScalerDoesNothing() {
    std::printf("[downscale] an unconfigured scaler writes nothing...\n");
    RgbDownscaler scaler;
    Check(!scaler.ready(), "a fresh scaler is not ready");

    std::vector<uint8_t> src(kPackedPixelBytes, 0x11);
    std::vector<uint8_t> dst(kPackedPixelBytes, 0);
    scaler.Scale(src.data(), kPackedPixelBytes, dst.data(), kPackedPixelBytes);
    Check(dst[0] == 0, "the destination is left untouched");

    scaler.Configure(8, 8, 4, 4);
    Check(scaler.ready(), "configuring it makes it ready");
    scaler.Scale(nullptr, 0, dst.data(), kPackedPixelBytes);
    Check(dst[0] == 0, "a null source is refused instead of dereferenced");
}

void TestReconfigureIsDetected() {
    std::printf("[downscale] the cached geometry reports when it no longer applies...\n");
    RgbDownscaler scaler;
    scaler.Configure(1920, 1080, 960, 540);
    Check(scaler.Matches(1920, 1080, 960, 540), "the geometry it was configured for matches");
    Check(!scaler.Matches(1920, 1080, 640, 360), "a different target does not");
    Check(!scaler.Matches(3440, 1440, 960, 540), "a different source does not");
}

}

void RunRgbDownscaleTests() {
    TestFourPixelsAverageIntoOne();
    TestSameSizeIsAFaithfulCopy();
    TestFlatFieldStaysFlat();
    TestNoSampleFallsOutsideTheSource();
    TestUpscaleIsRefused();
    TestUnconfiguredScalerDoesNothing();
    TestReconfigureIsDetected();
}
