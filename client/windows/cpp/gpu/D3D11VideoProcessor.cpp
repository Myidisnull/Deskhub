#include "gpu/D3D11VideoProcessor.h"

#include <d3d11_1.h>

#include "gpu/HrCheck.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr DXGI_COLOR_SPACE_TYPE ModernColorSpace(bool ycbcr) {
    return ycbcr ? DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709
                 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
}

D3D11_VIDEO_PROCESSOR_COLOR_SPACE LegacyColorSpace(bool ycbcr) {
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE cs{};
    if (ycbcr) {
        cs.YCbCr_Matrix = 1;
        cs.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
    }
    return cs;
}

}

void D3D11VideoProcessor::Reset() {
    inViews_.clear();
    outView_.Reset();
    vp_.Reset();
    vpEnum_.Reset();
    videoContext_.Reset();
    videoDevice_.Reset();
}

bool D3D11VideoProcessor::ConfigureSteps(ID3D11Device* device, ID3D11Texture2D* outputTarget,
    const Setup& s) {
    ComPtr<ID3D11Device> dev(device);
    ComPtr<ID3D11DeviceContext> ctx;
    dev->GetImmediateContext(&ctx);
    DH_HR_CHECK(tag_, dev.As(&videoDevice_), "ID3D11VideoDevice");
    DH_HR_CHECK(tag_, ctx.As(&videoContext_), "ID3D11VideoContext");

    D3D11_VIDEO_PROCESSOR_CONTENT_DESC cd{};
    cd.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    cd.InputWidth = s.srcWidth;
    cd.InputHeight = s.srcHeight;
    cd.OutputWidth = s.dstWidth;
    cd.OutputHeight = s.dstHeight;
    cd.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
    DH_HR_CHECK(tag_, videoDevice_->CreateVideoProcessorEnumerator(&cd, &vpEnum_),
        "CreateVideoProcessorEnumerator");

    if (s.requiredOutputFormat != DXGI_FORMAT_UNKNOWN) {
        UINT fmtFlags = 0;
        const HRESULT hr = vpEnum_->CheckVideoProcessorFormat(s.requiredOutputFormat, &fmtFlags);
        if (FAILED(hr) || !(fmtFlags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)) {
            LOGE("[%s] The GPU video processor cannot output format %d.", tag_,
                int(s.requiredOutputFormat));
            return false;
        }
    }

    DH_HR_CHECK(tag_, videoDevice_->CreateVideoProcessor(vpEnum_.Get(), 0, &vp_),
        "CreateVideoProcessor");

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC od{};
    od.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    od.Texture2D.MipSlice = 0;
    DH_HR_CHECK(tag_,
        videoDevice_->CreateVideoProcessorOutputView(outputTarget, vpEnum_.Get(), &od,
            &outView_),
        "CreateVideoProcessorOutputView");

    videoContext_->VideoProcessorSetStreamSourceRect(vp_.Get(), 0, TRUE, &s.srcRect);
    videoContext_->VideoProcessorSetStreamDestRect(vp_.Get(), 0, TRUE, &s.dstRect);

    ComPtr<ID3D11VideoContext1> vc1;
    if (SUCCEEDED(videoContext_.As(&vc1))) {
        vc1->VideoProcessorSetStreamColorSpace1(vp_.Get(), 0, ModernColorSpace(s.srcYCbCr));
        vc1->VideoProcessorSetOutputColorSpace1(vp_.Get(), ModernColorSpace(s.dstYCbCr));
    }
    const D3D11_VIDEO_PROCESSOR_COLOR_SPACE inCs = LegacyColorSpace(s.srcYCbCr);
    videoContext_->VideoProcessorSetStreamColorSpace(vp_.Get(), 0, &inCs);
    const D3D11_VIDEO_PROCESSOR_COLOR_SPACE outCs = LegacyColorSpace(s.dstYCbCr);
    videoContext_->VideoProcessorSetOutputColorSpace(vp_.Get(), &outCs);
    videoContext_->VideoProcessorSetStreamAutoProcessingMode(vp_.Get(), 0, FALSE);
    return true;
}

bool D3D11VideoProcessor::Configure(ID3D11Device* device, ID3D11Texture2D* outputTarget,
    const Setup& setup, const char* tag) {
    Reset();
    if (tag) tag_ = tag;
    if (!device || !outputTarget) return false;
    if (!ConfigureSteps(device, outputTarget, setup)) {
        Reset();
        return false;
    }
    return true;
}

bool D3D11VideoProcessor::Blt(ID3D11Texture2D* src, UINT subresource) {
    if (!src || !vp_ || !outView_) return false;

    const auto key = std::make_pair(src, subresource);
    auto it = inViews_.find(key);
    if (it == inViews_.end()) {
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC vd{};
        vd.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        vd.Texture2D.MipSlice = 0;
        vd.Texture2D.ArraySlice = subresource;
        ComPtr<ID3D11VideoProcessorInputView> view;
        DH_HR_CHECK(tag_,
            videoDevice_->CreateVideoProcessorInputView(src, vpEnum_.Get(), &vd, &view),
            "CreateVideoProcessorInputView");
        it = inViews_.emplace(key, std::move(view)).first;
    }

    D3D11_VIDEO_PROCESSOR_STREAM stream{};
    stream.Enable = TRUE;
    stream.pInputSurface = it->second.Get();
    DH_HR_CHECK(tag_,
        videoContext_->VideoProcessorBlt(vp_.Get(), outView_.Get(), 0, 1, &stream),
        "VideoProcessorBlt");
    return true;
}
