#pragma once
// =============================================================================
// PanelRenderer.h — vẽ NV12 vào một COMPOSITION swapchain, để gắn vào WinUI3
// SwapChainPanel. Bản không-cửa-sổ của Renderer (decode/Renderer.h).
//
// KHÁC Renderer Ở ĐÂU
//   Renderer tự sở hữu một HWND top-level + overlay + swapchain-for-HWND. PanelRenderer
//   KHÔNG có cửa sổ nào: nó tạo swapchain-for-COMPOSITION và trả con trỏ ra ngoài để
//   phía C# gắn vào SwapChainPanel qua ISwapChainPanelNative::SetSwapChain. Toàn bộ
//   phần chuyển màu NV12→BGRA bằng D3D11 Video Processor giữ nguyên như Renderer.
//
// VÌ SAO KHÔNG BIẾT CỠ VIDEO LÚC TẠO
//   Kích thước thật chỉ biết sau khi đàm phán/nhận frame đầu. Tạo swapchain cỡ tạm,
//   rồi ResizeBuffers ở frame đầu (và mỗi lần host đổi độ phân giải). SwapChainPanel
//   tự co giãn khung theo layout; giữ tỷ lệ là việc của phía C# (đặt aspect cho panel
//   từ callback báo cỡ).
//
// MÔ HÌNH LUỒNG
//   Init/SwapChain gọi trên thread tạo (UI của C#). RenderNV12 gọi trên thread decode.
//   renderMutex bảo vệ swapchain giữa hai bên; nhưng thực tế dh_client_stop join thread
//   decode TRƯỚC khi hủy PanelRenderer nên không có tranh chấp hủy.
//
// LIÊN QUAN: decode/Renderer.cpp (bản có cửa sổ — nguồn gốc đoạn Video Processor),
//            ClientApi.cpp (người dùng), client/windows/csharp Viewer (gắn swapchain)
// =============================================================================
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

    // Tạo composition swapchain trên `device` dùng chung với decoder. `initialW/H` chỉ
    // là cỡ tạm — frame đầu sẽ ResizeBuffers về cỡ thật. Trả false nếu lỗi.
    bool Init(ID3D11Device* device, uint32_t initialW, uint32_t initialH);

    // Con trỏ IDXGISwapChain1 (IUnknown*) để C# gắn vào SwapChainPanel. NULL nếu chưa Init.
    void* SwapChain() const;

    // Vẽ một frame NV12 (texture + array slice từ decoder). Tự ResizeBuffers nếu w/h
    // khác cỡ backbuffer hiện tại (host đổi độ phân giải). Gọi trên thread decode.
    bool RenderNV12(ID3D11Texture2D* tex, unsigned subresource, uint32_t width, uint32_t height);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
