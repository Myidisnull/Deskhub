// =============================================================================
// Downscaler.cpp — cài đặt. Vì sao lớp này tồn tại và vì sao nó nằm giữa capture và
// encoder: xem Downscaler.h. Ở đây chỉ còn phần dựng video processor.
//
// KHUÔN DỰNG LẤY TỪ MfEncoder::SetupColorConvert — cùng một trình tự
// (enumerator → processor → texture đích → output view → rect → color space), khác
// đúng hai điểm:
//   1. Đầu ra là BGRA chứ không phải NV12: đây là bước CO, không phải bước đổi màu.
//      Đổi màu vẫn là việc của encoder, ngay sau đây.
//   2. SourceRect lấy theo cỡ VÀO thật. MfEncoder đặt cả hai rect theo cỡ RA — với
//      nó thì vô hại vì hai cỡ luôn bằng nhau, nhưng ở đây hai cỡ khác nhau là lẽ
//      sống của cả file, và đặt SourceRect theo cỡ ra sẽ CẮT một góc khung rồi kéo
//      nó ra full màn hình thay vì co toàn khung.
// =============================================================================
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
    // Thứ tự NGƯỢC với lúc dựng. inViews_ phải đi trước vpEnum_: mỗi view do
    // enumerator đó validate, và giữ view sống lâu hơn enumerator là đúng loại lỗi
    // vòng đời COM chỉ nổ trên một số driver.
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
    // Không phóng to: xem Downscaler.h. Kẹp thay vì từ chối — người gọi ở trên đã
    // quyết định xong chính sách (StreamSize/QualityLadder), ở đây chỉ thi hành, và
    // trả false vì một phép làm tròn lẻ sẽ giết cả nguồn.
    if (dstW > srcW) dstW = srcW;
    if (dstH > srcH) dstH = srcH;
    dstW &= ~1u; // NV12 ở tầng dưới đòi cạnh chẵn
    dstH &= ~1u;
    if (!dstW || !dstH) return false;

    if (device == device_.Get() && srcW == srcW_ && srcH == srcH_ && dstW == dstW_ &&
        dstH == dstH_)
        return true; // no-op: đường nóng gọi mỗi frame

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

    // Hỏi trước thay vì để CreateVideoProcessorOutputView hỏng sau: thông báo "GPU
    // không co được BGRA" nói đúng vấn đề, còn một HRESULT ở giữa đường nóng thì
    // không.
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
    // RENDER_TARGET cho video processor ghi vào; SHADER_RESOURCE vì NVENC đăng ký
    // texture này làm input resource và một số driver đòi cờ đó. Texture của WGC
    // cũng mang đúng cặp cờ này, nên hai encoder nhận được thứ chúng vẫn quen.
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    DS_CHECK(device_->CreateTexture2D(&td, nullptr, &dstTex_), "CreateTexture2D(BGRA)");

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC od{};
    od.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    od.Texture2D.MipSlice = 0;
    DS_CHECK(videoDevice_->CreateVideoProcessorOutputView(dstTex_.Get(), vpEnum_.Get(), &od,
                 &outView_),
        "CreateVideoProcessorOutputView");

    // ⚠ SourceRect theo cỡ VÀO, DestRect theo cỡ RA — xem chú thích đầu file.
    RECT srcRect{0, 0, (LONG)srcW, (LONG)srcH};
    RECT dstRect{0, 0, (LONG)dstW, (LONG)dstH};
    videoContext_->VideoProcessorSetStreamSourceRect(vp_.Get(), 0, TRUE, &srcRect);
    videoContext_->VideoProcessorSetStreamDestRect(vp_.Get(), 0, TRUE, &dstRect);

    // Vào RGB full range, ra CŨNG RGB full range: đây thuần tuý là phép co, không
    // đổi không gian màu. Vẫn phải khai TƯỜNG MINH cả hai đầu — bỏ trống thì driver
    // tự đoán, và nếu nó đoán đầu ra là YUV limited thì bước đổi màu của encoder
    // ngay sau đây sẽ nén dải một lần NỮA (đen bị nâng, sáng bị cắt). Cùng bài học
    // đã đo được ở MfEncoder::SetupColorConvert 24/07/2026: driver đời mới chỉ tôn
    // trọng đường ColorSpace1, driver cũ chỉ hiểu struct legacy — nên gọi cả hai.
    ComPtr<ID3D11VideoContext1> vc1;
    if (SUCCEEDED(videoContext_.As(&vc1))) {
        vc1->VideoProcessorSetStreamColorSpace1(vp_.Get(), 0,
            DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
        vc1->VideoProcessorSetOutputColorSpace1(vp_.Get(),
            DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    }
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE cs{};
    cs.RGB_Range = 0; // 0 = full 0-255
    videoContext_->VideoProcessorSetStreamColorSpace(vp_.Get(), 0, &cs);
    videoContext_->VideoProcessorSetOutputColorSpace(vp_.Get(), &cs);
    // Tắt mọi "cải thiện" tự động của driver (khử nhiễu, làm nét). Nội dung màn hình
    // là chữ và đường thẳng — bộ khử nhiễu làm chữ nhoè, bộ làm nét tạo viền quanh
    // chữ, và cả hai đều tốn bit của encoder cho thứ không ai muốn.
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

    // Input view nhớ theo con trỏ texture — xem inViews_ ở header. WGC luân phiên
    // đúng vài texture cố định nên map này bão hoà sau vài frame.
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
