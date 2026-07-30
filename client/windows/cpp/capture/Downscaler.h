#pragma once
// =============================================================================
// Downscaler.h — co một texture BGRA xuống cỡ nhỏ hơn, trên GPU, TRƯỚC encoder.
//
// NHIỆM VỤ
//   Nhận texture WGC (BGRA, cỡ màn hình thật) và trả một texture BGRA nhỏ hơn theo
//   cỡ ta chọn. Không đổi định dạng, không đổi không gian màu — chỉ đổi kích thước.
//
// VỊ TRÍ TRONG LUỒNG DỮ LIỆU
//   ScreenCapture (WGC) → **Downscaler** → IVideoEncoder → Packetizer → UDP
//
// VÌ SAO NẰM Ở ĐÂY CHỨ KHÔNG PHẢI TRONG ENCODER
//   Có HAI encoder và chỉ một trong hai co được:
//     - MfEncoder đã có sẵn một D3D11 video processor (BGRA→NV12) nên về lý thuyết
//       co được luôn ở đó.
//     - NvencEncoder thì KHÔNG. Nó đăng ký thẳng texture của WGC vào NVENC
//       (NvEncRegisterResource với đúng encodeWidth) và NVENC không có bộ co.
//   Đặt bộ co trong MfEncoder nghĩa là trần độ phân giải chạy trên máy Intel/AMD và
//   im lặng không chạy trên máy NVIDIA — một tính năng "có mà như không" tuỳ card,
//   đúng loại lỗi người dùng không bao giờ báo đúng. Đặt ở đây thì cả hai encoder
//   nhận đầu vào đã đúng cỡ và không encoder nào phải biết chuyện gì đã xảy ra.
//
// VÌ SAO VIDEO PROCESSOR CHỨ KHÔNG PHẢI SHADER
//   Cùng lý do MfEncoder dùng nó cho đổi màu: đây là khối phần cứng cố định, không
//   phải cấp phát pipeline state / vertex buffer / sampler nào, và chất lượng co của
//   nó tốt hơn một lần lấy mẫu bilinear thủ công. Đổi lại phải khai TƯỜNG MINH
//   color space ở cả hai đầu (xem .cpp) — thiếu là màu trôi cả khung.
//
// ⚠ TEXTURE TRẢ VỀ THUỘC VỀ Downscaler
//   Nó được dùng lại qua từng frame (không cấp phát gì trên đường nóng) nên chỉ hợp
//   lệ tới lời gọi Scale() kế tiếp. Đúng cùng quy tắc vòng đời như texture của WGC
//   (xem CaptureTypes.h) — bên tiêu thụ phải encode/copy ngay.
//
// LIÊN QUAN: capture/CaptureTypes.h, encode/MfEncoder.cpp (SetupColorConvert — cùng
//            khuôn dựng video processor), deskhub/control/StreamSize.h (bên QUYẾT
//            ĐỊNH co xuống bao nhiêu; file này chỉ thi hành)
// =============================================================================
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

    // Dựng lại cho một cặp (cỡ vào, cỡ ra). Gọi lại với cùng bốn số là no-op, nên
    // đường nóng gọi thoải mái. false = GPU không dựng nổi video processor.
    //
    // Cỡ ra phải CHẴN (ràng buộc NV12 ở tầng dưới) và không lớn hơn cỡ vào — phóng
    // to ở host chỉ đốt bitrate cho pixel nội suy mà client tự làm được.
    bool Configure(ID3D11Device* device, uint32_t srcW, uint32_t srcH, uint32_t dstW,
        uint32_t dstH);

    // Co `src` xuống cỡ đã cấu hình. Trả texture kết quả (thuộc về Downscaler, chỉ
    // hợp lệ tới lời gọi Scale kế tiếp), hoặc nullptr nếu hỏng.
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

    // Input view nhớ theo CON TRỎ texture: WGC luân phiên đúng vài texture cố định
    // (frame pool sâu 2), nên map này bão hoà sau vài frame và đường nóng không còn
    // tạo view nữa. Cùng thủ thuật với `vpInViews` của MfEncoder và `registered`
    // của NvencEncoder.
    std::map<ID3D11Texture2D*, Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView>> inViews_;

    uint32_t srcW_ = 0, srcH_ = 0, dstW_ = 0, dstH_ = 0;
};
