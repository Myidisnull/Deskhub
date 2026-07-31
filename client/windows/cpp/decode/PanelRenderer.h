#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <d3d11.h>
#include <cstdint>
#include <memory>

class PanelRenderer {
public:
    PanelRenderer();
    ~PanelRenderer();
    PanelRenderer(const PanelRenderer&) = delete;
    PanelRenderer& operator=(const PanelRenderer&) = delete;

    bool InitForHwnd(ID3D11Device* device, void* hwnd, uint32_t initialW, uint32_t initialH);

    bool RenderNV12(ID3D11Texture2D* tex, unsigned subresource, uint32_t width, uint32_t height,
        uint64_t* outReadyUs = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
