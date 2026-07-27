#pragma once
// =============================================================================
// PanelRenderer.h — vẽ NV12 vào một swapchain gắn với cửa sổ CON do app cấp
// (InitForHwnd, đường duy nhất còn dùng — app win32/Viewer.cpp). Bản
// không-tự-sở-hữu-cửa-sổ của Renderer (decode/Renderer.h).
//
// KHÁC Renderer Ở ĐÂU
//   Renderer tự sở hữu một HWND top-level + overlay. PanelRenderer nhận HWND từ
//   ngoài và chỉ lo swapchain + chuyển màu NV12→BGRA bằng D3D11 Video Processor
//   (giữ nguyên như Renderer). (Đường composition cho SwapChainPanel của frontend
//   C# cũ đã xoá 2026-07-27 cùng frontend đó.)
//
// VÌ SAO KHÔNG BIẾT CỠ VIDEO LÚC TẠO
//   Kích thước thật chỉ biết sau khi đàm phán/nhận frame đầu. Tạo swapchain cỡ tạm,
//   rồi ResizeBuffers ở frame đầu (và mỗi lần host đổi độ phân giải). SwapChainPanel
//   tự co giãn khung theo layout; giữ tỷ lệ là việc của phía C# (đặt aspect cho panel
//   từ callback báo cỡ).
//
// MÔ HÌNH LUỒNG
//   InitForHwnd gọi trên thread tạo (UI). RenderNV12 gọi trên thread decode.
//   renderMutex bảo vệ swapchain giữa hai bên; nhưng thực tế dh_client_stop join thread
//   decode TRƯỚC khi hủy PanelRenderer nên không có tranh chấp hủy.
//
// LIÊN QUAN: decode/Renderer.cpp (bản có cửa sổ — nguồn gốc đoạn Video Processor),
//            ClientApi.cpp (người dùng), client/windows/win32/Viewer.cpp
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

    // Tạo swapchain-for-HWND gắn vào cửa sổ CON do app đưa xuống, trên `device` dùng
    // chung với decoder. `initialW/H` chỉ là cỡ tạm — frame đầu sẽ ResizeBuffers về
    // cỡ thật. Backbuffer = cỡ video (DXGI_SCALING_STRETCH kéo ra cỡ cửa sổ); giữ tỷ
    // lệ là việc của app — đặt cỡ child HWND theo khung video. Trả false nếu lỗi.
    bool InitForHwnd(ID3D11Device* device, void* hwnd, uint32_t initialW, uint32_t initialH);

    // Vẽ một frame NV12 (texture + array slice từ decoder). Tự ResizeBuffers nếu w/h
    // khác cỡ backbuffer hiện tại (host đổi độ phân giải). Gọi trên thread decode.
    bool RenderNV12(ID3D11Texture2D* tex, unsigned subresource, uint32_t width, uint32_t height);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
