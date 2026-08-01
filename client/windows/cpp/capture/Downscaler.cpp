#include "capture/Downscaler.h"

#include "deskhub/control/StreamSize.h"
#include "deskhubp/diag/Log.h"
#include "gpu/HrCheck.h"

using Microsoft::WRL::ComPtr;

void Downscaler::Reset() {
    vp_.Reset();
    dstTex_.Reset();
    device_.Reset();
    srcW_ = srcH_ = dstW_ = dstH_ = 0;
}

bool Downscaler::Configure(ID3D11Device* device, uint32_t srcW, uint32_t srcH, uint32_t dstW,
    uint32_t dstH) {
    if (!device || !srcW || !srcH || !dstW || !dstH) return false;
    if (dstW > srcW) dstW = srcW;
    if (dstH > srcH) dstH = srcH;
    dstW = deskhub::EvenDown(dstW);
    dstH = deskhub::EvenDown(dstH);
    if (!dstW || !dstH) return false;

    if (device == device_.Get() && srcW == srcW_ && srcH == srcH_ && dstW == dstW_ &&
        dstH == dstH_)
        return true;

    Reset();

    D3D11_TEXTURE2D_DESC td{};
    td.Width = dstW;
    td.Height = dstH;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> tex;
    DH_HR_CHECK("Downscaler", device->CreateTexture2D(&td, nullptr, &tex),
        "CreateTexture2D(BGRA)");

    D3D11VideoProcessor::Setup s;
    s.srcWidth = srcW;
    s.srcHeight = srcH;
    s.dstWidth = dstW;
    s.dstHeight = dstH;
    s.srcRect = RECT{0, 0, (LONG)srcW, (LONG)srcH};
    s.dstRect = RECT{0, 0, (LONG)dstW, (LONG)dstH};
    s.requiredOutputFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    if (!vp_.Configure(device, tex.Get(), s, "Downscaler")) return false;

    device_ = device;
    dstTex_ = std::move(tex);
    srcW_ = srcW;
    srcH_ = srcH;
    dstW_ = dstW;
    dstH_ = dstH;
    LOGI("[Downscaler] %ux%u -> %ux%u (GPU video processor).", srcW, srcH, dstW, dstH);
    return true;
}

ID3D11Texture2D* Downscaler::Scale(ID3D11Texture2D* src) {
    if (!src || !dstTex_) return nullptr;
    return vp_.Blt(src) ? dstTex_.Get() : nullptr;
}
