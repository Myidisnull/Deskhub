#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "encode/IVideoEncoder.h"
#include "encode/NvencEncoder.h"
#include "encode/MfEncoder.h"

#include <cstdio>

std::unique_ptr<IVideoEncoder> CreateEncoder(ID3D11Device* device, const EncoderConfig& cfg) {
    {
        auto enc = std::make_unique<NvencEncoder>();
        if (enc->Init(device, cfg)) {
            std::printf("[Encoder] Using backend: %s\n", enc->BackendName());
            return enc;
        }
        std::printf("[Encoder] NVENC unavailable, trying Media Foundation...\n");
    }
    {
        auto enc = std::make_unique<MfEncoder>();
        if (enc->Init(device, cfg)) {
            std::printf("[Encoder] Using backend: %s\n", enc->BackendName());
            return enc;
        }
    }
    std::printf("[Encoder] Failed to initialize any backend.\n");
    return nullptr;
}
