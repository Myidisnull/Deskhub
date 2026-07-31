#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <cstdint>
#include <functional>
#include <memory>

#include "encode/IVideoEncoder.h"

using deskhub::media::DecoderConfig;

struct DecodedFrame {
    ID3D11Texture2D* texture;
    UINT subresource;
    uint32_t width;
    uint32_t height;
    uint64_t timestampUs;
};

class IVideoDecoder {
public:
    using FrameHandler = std::function<void(const DecodedFrame&)>;

    virtual ~IVideoDecoder() = default;

    virtual bool Init(ID3D11Device* device, const DecoderConfig& cfg,
        FrameHandler onFrame) = 0;

    virtual bool Decode(const uint8_t* data, size_t size, uint64_t timestampUs) = 0;

    virtual const char* BackendName() const = 0;
};

std::unique_ptr<IVideoDecoder> CreateDecoder(ID3D11Device* device, const DecoderConfig& cfg,
    IVideoDecoder::FrameHandler onFrame);

static_assert(deskhub::media::VideoDecoderLike<IVideoDecoder>,
    "IVideoDecoder must match the shared decoder signature");
