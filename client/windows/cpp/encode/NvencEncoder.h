#pragma once
#include "encode/IVideoEncoder.h"

class NvencEncoder : public IVideoEncoder {
public:
    NvencEncoder();
    ~NvencEncoder() override;

    bool Init(ID3D11Device* device, const EncoderConfig& cfg) override;
    bool Encode(ID3D11Texture2D* frame, uint64_t timestampUs, bool forceKeyframe) override;
    bool SetBitrate(uint32_t bitrateBps) override;
    bool SetFps(uint32_t fps) override;
    void Finish() override;
    const char* BackendName() const override {
        return "NVENC (NVIDIA)";
    }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
