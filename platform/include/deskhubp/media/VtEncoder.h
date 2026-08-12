#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

#include "deskhub/media/VideoContract.h"

using deskhub::media::EncoderConfig;
using deskhub::media::PacketHandler;

class VtEncoder {
public:
    VtEncoder() = default;
    ~VtEncoder();
    VtEncoder(const VtEncoder&) = delete;
    VtEncoder& operator=(const VtEncoder&) = delete;

    bool Init(const EncoderConfig& cfg);

    bool Encode(void* pixelBuffer, uint64_t timestampUs, bool forceKeyframe);

    bool SetBitrate(uint32_t bitrateBps);

    bool SetFps(uint32_t fps);

    void Flush();

    void Finish();

    bool IsOpen() const {
        return session_ != nullptr;
    }
    const char* BackendName() const {
        return hardware_ ? "VideoToolbox (hardware)" : "VideoToolbox (software)";
    }

    void OnEncoded(void* sampleBuffer, int32_t status, uint32_t infoFlags);

private:
    void* session_ = nullptr;
    EncoderConfig cfg_{};
    bool hardware_ = false;

    std::mutex emitMutex_;
    std::vector<uint8_t> annexb_;
};

static_assert(deskhub::media::VideoEncoderLike<VtEncoder, void*>,
    "VtEncoder must match the shared encoder signature");
static_assert(deskhub::media::HotFpsEncoder<VtEncoder>,
    "VtEncoder can change fps on the fly — VideoToolbox has ExpectedFrameRate");
