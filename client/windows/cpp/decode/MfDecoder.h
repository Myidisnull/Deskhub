#pragma once
#include "decode/IVideoDecoder.h"

class MfDecoder : public IVideoDecoder {
public:
    MfDecoder();
    ~MfDecoder() override;

    bool Init(ID3D11Device* device, const DecoderConfig& cfg,
        FrameHandler onFrame) override;
    bool Decode(const uint8_t* data, size_t size, uint64_t timestampUs) override;
    const char* BackendName() const override {
        return "Media Foundation (D3D11VA)";
    }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
