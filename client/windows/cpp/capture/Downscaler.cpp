#include "capture/Downscaler.h"

#include <d3d11_1.h>
#include <cstdio>

using Microsoft::WRL::ComPtr;

#define DS_CHECK(expr, msg)                                         \
    do {                                                            \
        HRESULT _hr = (expr);                                       \
        if (FAILED(_hr)) {                                          \
            std::printf("[Downscaler] %s failed: 0x%08lX\n", (msg), \
                (unsigned long)_hr);                                \
            Reset();                                                \
            return false;                                           \
        }                                                           \
    } while (0)

void Downscaler::Reset() {
    inViews_.clear();
    outView_.Reset();
    dstTex_.Reset();
    vp_.Reset();
    vpEnum_.Reset();
    videoContext_.Reset();
    videoDevice_.Reset();
    context_.Reset();
    device_.Reset();
    srcW_ = srcH_ = dstW_ = dstH_ = 0;
}

bool Downscaler::Configure(ID3D11Device* device, uint32_t srcW, uint32_t srcH, uint32_t dstW,
    uint32_t dstH) {
    if (!device || !srcW || !srcH || !dstW || !dstH) return false;
    if (dstW > srcW) dstW = srcW;
    if (dstH > srcH) dstH = srcH;
    dstW &= ~1u;
    dstH &= ~1u;
    if (!dstW || !dstH) return false;

    if (device == device_.Get() && srcW == srcW_ && srcH == srcH_ && dstW == dstW_ &&
        dstH == dstH_)
        return true;

    Reset();
    device_ = device;
    device_->GetImmediateContext(&context_);

    DS_CHECK(device_.As(&videoDevice_), "ID3D11VideoDevice");
    DS_CHECK(context_.As(&videoContext_), "ID3D11VideoContext");

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC cd{};
    cd.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    cd.InputWidth = srcW;
    cd.InputHeight = srcH;
    cd.OutputWidth = dstW;
    cd.OutputHeight = dstH;
    cd.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    DS_CHECK(videoDevice_->CreateVideoProcessorEnumerator(&cd, &vpEnum_),
        "CreateVideoProcessorEnumerator");

    UINT fmtFlags = 0;
    HRESULT hr = vpEnum_->CheckVideoProcessorFormat(DXGI_FORMAT_B8G8R8A8_UNORM, &fmtFlags);
    if (FAILED(hr) || !(fmtFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)) {
        std::printf("[Downscaler] GPU cannot output BGRA from a video processor.\n");
        Reset();
        return false;
    }

    DS_CHECK(videoDevice_->CreateVideoProcessor(vpEnum_.Get(), 0, &vp_), "CreateVideoProcessor");

    D3D11_TEXTURE2D_DESC td{};
    td.Width = dstW;
    td.Height = dstH;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    DS_CHECK(device_->CreateTexture2D(&td, nullptr, &dstTex_), "CreateTexture2D(BGRA)");

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC od{};
    od.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    od.Texture2D.MipSlice = 0;
    DS_CHECK(videoDevice_->CreateVideoProcessorOutputView(dstTex_.Get(), vpEnum_.Get(), &od,
                 &outView_),
        "CreateVideoProcessorOutputView");

    RECT srcRect{0, 0, (LONG)srcW, (LONG)srcH};
    RECT dstRect{0, 0, (LONG)dstW, (LONG)dstH};
    videoContext_->VideoProcessorSetStreamSourceRect(vp_.Get(), 0, TRUE, &srcRect);
    videoContext_->VideoProcessorSetStreamDestRect(vp_.Get(), 0, TRUE, &dstRect);

    ComPtr<ID3D11VideoContext1> vc1;
    if (SUCCEEDED(videoContext_.As(&vc1))) {
        vc1->VideoProcessorSetStreamColorSpace1(vp_.Get(), 0,
            DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        vc1->VideoProcessorSetOutputColorSpace1(vp_.Get(),
            DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    }
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE cs{};
    cs.RGB_Range = 0;
    videoContext_->VideoProcessorSetStreamColorSpace(vp_.Get(), 0, &cs);
    videoContext_->VideoProcessorSetOutputColorSpace(vp_.Get(), &cs);
    videoContext_->VideoProcessorSetStreamAutoProcessingMode(vp_.Get(), 0, FALSE);

    srcW_ = srcW;
    srcH_ = srcH;
    dstW_ = dstW;
    dstH_ = dstH;
    std::printf("[Downscaler] %ux%u -> %ux%u (GPU video processor).\n", srcW, srcH, dstW, dstH);
    return true;
}

ID3D11Texture2D* Downscaler::Scale(ID3D11Texture2D* src) {
    if (!src || !vp_ || !outView_) return nullptr;

    auto it = inViews_.find(src);
    if (it == inViews_.end()) {
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC vd{};
        vd.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        vd.Texture2D.MipSlice = 0;
        ComPtr<ID3D11VideoProcessorInputView> view;
        const HRESULT hr =
            videoDevice_->CreateVideoProcessorInputView(src, vpEnum_.Get(), &vd, &view);
        if (FAILED(hr)) {
            std::printf("[Downscaler] CreateVideoProcessorInputView failed: 0x%08lX\n",
                (unsigned long)hr);
            return nullptr;
        }
        it = inViews_.emplace(src, std::move(view)).first;
    }

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = it->second.Get();
    const HRESULT hr = videoContext_->VideoProcessorBlt(vp_.Get(), outView_.Get(), 0, 1, &stream);
    if (FAILED(hr)) {
        std::printf("[Downscaler] VideoProcessorBlt failed: 0x%08lX\n", (unsigned long)hr);
        return nullptr;
    }
    return dstTex_.Get();
}
