#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "encode/IVideoEncoder.h"
#include "encode/NvencEncoder.h"
#include "encode/MfEncoder.h"

#include <cstdio>

#include "deskhubp/diag/Log.h"

std::unique_ptr<IVideoEncoder> CreateEncoder(ID3D11Device* device, const EncoderConfig& cfg) {
    {
        auto enc = std::make_unique<NvencEncoder>();
        if (enc->Init(device, cfg)) {
            LOGI("[Encoder] Using backend: %s", enc->BackendName());
            return enc;
        }
        LOGW("[Encoder] NVENC unavailable, trying Media Foundation...");
    }
    {
        auto enc = std::make_unique<MfEncoder>();
        if (enc->Init(device, cfg)) {
            LOGI("[Encoder] Using backend: %s", enc->BackendName());
            return enc;
        }
    }
    LOGE("[Encoder] Failed to initialize any backend.");
    return nullptr;
}
