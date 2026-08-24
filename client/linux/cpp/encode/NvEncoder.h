#pragma once
#include <cstdint>
#include <memory>

#include "capture/CaptureTypes.h"

#include "deskhub/media/VideoContract.h"
#include "deskhub/media/VideoTypes.h"

class NvEncoder {
public:
    NvEncoder();
    ~NvEncoder();
    NvEncoder(const NvEncoder&) = delete;
    NvEncoder& operator=(const NvEncoder&) = delete;

    static bool DriverPresent();

    bool Init(const deskhub::media::EncoderConfig& cfg, uint32_t drmFormat);

    bool Encode(const LinuxFrameInfo& fi, uint64_t timestampUs, bool forceKeyframe);

    bool EncodeLast(uint64_t timestampUs, bool forceKeyframe);

    bool haveSourceFrame() const;

    bool SetBitrate(uint32_t bitrateBps);

    void Finish();

    bool IsOpen() const;

    const char* BackendName() const {
        return "NVENC (hardware)";
    }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

static_assert(deskhub::media::VideoEncoderLike<NvEncoder, const LinuxFrameInfo&>,
    "NvEncoder must match the shared encoder signature");
