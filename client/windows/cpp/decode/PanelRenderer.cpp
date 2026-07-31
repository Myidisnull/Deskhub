#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "decode/PanelRenderer.h"

#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdio>
#include <map>
#include <mutex>
#include <utility>

#include "deskhubp/Clock.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

#define PR_CHECK(expr, msg)                                            \
    do {                                                               \
        HRESULT _hr = (expr);                                          \
        if (FAILED(_hr)) {                                             \
            std::printf("[PanelRenderer] %s failed: 0x%08lX\n", (msg), \
                (unsigned long)_hr);                                   \
            return false;                                              \
        }                                                              \
    } while (0)

struct PanelRenderer::Impl {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain1> swapchain;
    ComPtr<ID3D11Texture2D> backbuffer;
    ComPtr<ID3D11VideoDevice> videoDevice;
    ComPtr<ID3D11VideoContext> videoContext;
    ComPtr<ID3D11VideoProcessorEnumerator> vpEnum;
    ComPtr<ID3D11VideoProcessor> vp;
    ComPtr<ID3D11VideoProcessorOutputView> outView;
    std::map<std::pair<ID3D11Texture2D*, UINT>, ComPtr<ID3D11VideoProcessorInputView>> inViews;
    std::mutex renderMutex;
    uint32_t bbW = 0, bbH = 0;
    uint32_t vpSrcW = 0, vpSrcH = 0;

    bool Init(ID3D11Device* dev, HWND hwnd, uint32_t initialW, uint32_t initialH) {
        if (!hwnd) return false;
        device = dev;
        device->GetImmediateContext(&context);
        ComPtr<ID3D10Multithread> mt;
        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&mt)))) mt->SetMultithreadProtected(TRUE);

        bbW = initialW ? initialW : 16;
        bbH = initialH ? initialH : 16;

        ComPtr<IDXGIDevice> dxgiDev;
        PR_CHECK(device.As(&dxgiDev), "IDXGIDevice");
        ComPtr<IDXGIAdapter> adapter;
        PR_CHECK(dxgiDev->GetAdapter(&adapter), "GetAdapter");
        ComPtr<IDXGIFactory2> factory;
        PR_CHECK(adapter->GetParent(IID_PPV_ARGS(&factory)), "GetParent(Factory2)");

        {
            ComPtr<IDXGIDevice1> dxgiDev1;
            if (SUCCEEDED(dxgiDev.As(&dxgiDev1))) dxgiDev1->SetMaximumFrameLatency(1);
        }

        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = bbW;
        sd.Height = bbH;
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        PR_CHECK(factory->CreateSwapChainForHwnd(device.Get(), hwnd, &sd, nullptr, nullptr,
                     &swapchain),
            "CreateSwapChainForHwnd");
        ComPtr<IDXGIFactory1> parent;
        if (SUCCEEDED(swapchain->GetParent(IID_PPV_ARGS(&parent))))
            parent->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
        PR_CHECK(swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)), "GetBuffer");

        PR_CHECK(device.As(&videoDevice), "ID3D11VideoDevice");
        PR_CHECK(context.As(&videoContext), "ID3D11VideoContext");
        return true;
    }

    bool Resize(uint32_t w, uint32_t h) {
        backbuffer.Reset();
        outView.Reset();
        PR_CHECK(swapchain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers");
        PR_CHECK(swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)), "GetBuffer(resize)");
        bbW = w;
        bbH = h;
        vpSrcW = vpSrcH = 0;
        return true;
    }

    bool EnsureVideoProcessor(uint32_t w, uint32_t h) {
        if (vp && w == vpSrcW && h == vpSrcH) return true;
        inViews.clear();
        outView.Reset();
        vp.Reset();
        vpEnum.Reset();

        D3D11_VIDEO_PROCESSOR_CONTENT_DESC cd{};
        cd.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        cd.InputWidth = w;
        cd.InputHeight = h;
        cd.OutputWidth = bbW;
        cd.OutputHeight = bbH;
        cd.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
        PR_CHECK(videoDevice->CreateVideoProcessorEnumerator(&cd, &vpEnum),
            "CreateVideoProcessorEnumerator");
        PR_CHECK(videoDevice->CreateVideoProcessor(vpEnum.Get(), 0, &vp), "CreateVideoProcessor");

        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC od{};
        od.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        od.Texture2D.MipSlice = 0;
        PR_CHECK(videoDevice->CreateVideoProcessorOutputView(backbuffer.Get(), vpEnum.Get(), &od,
                     &outView),
            "CreateVideoProcessorOutputView");

        RECT src{0, 0, (LONG)w, (LONG)h};
        videoContext->VideoProcessorSetStreamSourceRect(vp.Get(), 0, TRUE, &src);
        RECT dst{0, 0, (LONG)bbW, (LONG)bbH};
        videoContext->VideoProcessorSetStreamDestRect(vp.Get(), 0, TRUE, &dst);

        ComPtr<ID3D11VideoContext1> vc1;
        if (SUCCEEDED(videoContext.As(&vc1))) {
            vc1->VideoProcessorSetStreamColorSpace1(vp.Get(), 0,
                DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709);
            vc1->VideoProcessorSetOutputColorSpace1(vp.Get(),
                DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        }
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE inCs{};
        inCs.YCbCr_Matrix = 1;
        inCs.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
        videoContext->VideoProcessorSetStreamColorSpace(vp.Get(), 0, &inCs);
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE outCs{};
        outCs.RGB_Range = 0;
        videoContext->VideoProcessorSetOutputColorSpace(vp.Get(), &outCs);
        videoContext->VideoProcessorSetStreamAutoProcessingMode(vp.Get(), 0, FALSE);

        vpSrcW = w;
        vpSrcH = h;
        return true;
    }

    bool RenderNV12(ID3D11Texture2D* tex, UINT subresource, uint32_t w, uint32_t h,
        uint64_t* outReadyUs) {
        std::lock_guard<std::mutex> lk(renderMutex);
        if (!swapchain) return false;
        if ((w != bbW || h != bbH) && !Resize(w, h)) return false;
        if (!EnsureVideoProcessor(w, h)) return false;

        auto key = std::make_pair(tex, subresource);
        auto it = inViews.find(key);
        if (it == inViews.end()) {
            D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC vd{};
            vd.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
            vd.Texture2D.MipSlice = 0;
            vd.Texture2D.ArraySlice = subresource;
            ComPtr<ID3D11VideoProcessorInputView> view;
            PR_CHECK(videoDevice->CreateVideoProcessorInputView(tex, vpEnum.Get(), &vd, &view),
                "CreateVideoProcessorInputView");
            it = inViews.emplace(key, std::move(view)).first;
        }

        D3D11_VIDEO_PROCESSOR_STREAM stream{};
        stream.Enable = TRUE;
        stream.pInputSurface = it->second.Get();
        PR_CHECK(videoContext->VideoProcessorBlt(vp.Get(), outView.Get(), 0, 1, &stream),
            "VideoProcessorBlt");
        if (outReadyUs) *outReadyUs = NowUs();
        PR_CHECK(swapchain->Present(0, 0), "Present");
        return true;
    }
};

PanelRenderer::PanelRenderer() = default;
PanelRenderer::~PanelRenderer() = default;

bool PanelRenderer::InitForHwnd(ID3D11Device* device, void* hwnd, uint32_t initialW,
    uint32_t initialH) {
    impl_ = std::make_unique<Impl>();
    if (!impl_->Init(device, (HWND)hwnd, initialW, initialH)) {
        impl_.reset();
        return false;
    }
    return true;
}

bool PanelRenderer::RenderNV12(ID3D11Texture2D* tex, unsigned subresource, uint32_t width,
    uint32_t height, uint64_t* outReadyUs) {
    return impl_ && impl_->RenderNV12(tex, subresource, width, height, outReadyUs);
}
