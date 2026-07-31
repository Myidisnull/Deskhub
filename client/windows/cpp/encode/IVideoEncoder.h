#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "deskhub/media/VideoContract.h"

using deskhub::media::Codec;
using deskhub::media::PacketHandler;
using deskhub::media::RateControl;

struct EncoderConfig : deskhub::media::EncoderConfig {
    std::wstring outputPath = L"output.mp4";
};

class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;

    virtual bool Init(ID3D11Device* device, const EncoderConfig& cfg) = 0;

    virtual bool Encode(ID3D11Texture2D* frame, uint64_t timestampUs, bool forceKeyframe) = 0;

    virtual bool SetBitrate(uint32_t bitrateBps) = 0;

    virtual bool SetFps(uint32_t fps) = 0;

    virtual void Finish() = 0;

    virtual const char* BackendName() const = 0;
};

std::unique_ptr<IVideoEncoder> CreateEncoder(ID3D11Device* device, const EncoderConfig& cfg);

static_assert(deskhub::media::VideoEncoderLike<IVideoEncoder, ID3D11Texture2D*>,
    "IVideoEncoder must match the shared encoder signature");
static_assert(deskhub::media::HotFpsEncoder<IVideoEncoder>,
    "IVideoEncoder can change fps on the fly (NVENC reconfigure; MF rebuilds the transform)");
