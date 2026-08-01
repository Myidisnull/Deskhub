#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>
#include <utility>

class D3D11VideoProcessor {
public:
    struct Setup {
        uint32_t srcWidth = 0;
        uint32_t srcHeight = 0;
        uint32_t dstWidth = 0;
        uint32_t dstHeight = 0;
        RECT srcRect{};
        RECT dstRect{};
        bool srcYCbCr = false;
        bool dstYCbCr = false;
        DXGI_FORMAT requiredOutputFormat = DXGI_FORMAT_UNKNOWN;
    };

    D3D11VideoProcessor() = default;
    D3D11VideoProcessor(const D3D11VideoProcessor&) = delete;
    D3D11VideoProcessor& operator=(const D3D11VideoProcessor&) = delete;

    bool Configure(ID3D11Device* device, ID3D11Texture2D* outputTarget, const Setup& setup,
        const char* tag);

    bool Blt(ID3D11Texture2D* src, UINT subresource = 0);

    void Reset();

    bool ready() const {
        return vp_ != nullptr;
    }

private:
    bool ConfigureSteps(ID3D11Device* device, ID3D11Texture2D* outputTarget, const Setup& s);

    const char* tag_ = "D3D11VideoProcessor";
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> videoDevice_;
    Microsoft::WRL::ComPtr<ID3D11VideoContext> videoContext_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> vpEnum_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> vp_;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> outView_;
    std::map<std::pair<ID3D11Texture2D*, UINT>,
        Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView>>
        inViews_;
};
