#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>

class Downscaler {
public:
    Downscaler() = default;
    ~Downscaler() = default;
    Downscaler(const Downscaler&) = delete;
    Downscaler& operator=(const Downscaler&) = delete;

    bool Configure(ID3D11Device* device, uint32_t srcW, uint32_t srcH, uint32_t dstW,
        uint32_t dstH);

    ID3D11Texture2D* Scale(ID3D11Texture2D* src);

    uint32_t dstWidth() const {
        return dstW_;
    }
    uint32_t dstHeight() const {
        return dstH_;
    }

private:
    void Reset();

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> videoDevice_;
    Microsoft::WRL::ComPtr<ID3D11VideoContext> videoContext_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> vpEnum_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> vp_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> dstTex_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> outView_;

    std::map<ID3D11Texture2D*, Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView>> inViews_;

    uint32_t srcW_ = 0, srcH_ = 0, dstW_ = 0, dstH_ = 0;
};
