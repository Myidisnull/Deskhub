#pragma once
// =============================================================================
// VideoRenderer.h — đưa frame đã giải mã lên màn hình bằng OpenGL.
//                   Đối ứng client/windows/cpp/decode/PanelRenderer.h (D3D11).
//                   Trên macOS/iOS/Android KHÔNG có lớp này: ở đó "giải mã" và
//                   "hiển thị" là một thao tác (enqueue vào layer / releaseOutput
//                   Buffer vào Surface).
//
// VỊ TRÍ TRONG LUỒNG DỮ LIỆU
//   AvDecoder → **VideoRenderer** → GtkGLArea → compositor
//
// ⚠ HAI THREAD, RANH GIỚI RẤT RÕ — ĐỌC TRƯỚC KHI SỬA
//   Thread DECODE gọi: SubmitFrame / SetVaDisplay / DropFrames.
//     Đây là nơi DUY NHẤT chạm vào libva (vaExportSurfaceHandle) và libav.
//   Thread GTK (main) gọi: Realize / Unrealize / Render.
//     Đây là nơi DUY NHẤT chạm vào OpenGL và EGL.
//   Cắt đúng ở đây là có chủ ý: libva KHÔNG an toàn đa luồng, và context OpenGL
//   thì gắn chặt vào một thread. Gộp hai bên lại (ví dụ export dma-buf ngay trong
//   hàm vẽ) sẽ cần một khoá chung cho cả libva lẫn GL, và mọi lần vẽ sẽ chặn
//   đường giải mã.
//
// ⚠ ZERO-COPY BẰNG DMA-BUF
//   Frame phần cứng nằm trong VRAM dưới dạng VASurface. Ta KHÔNG kéo nó về RAM
//   (av_hwframe_transfer_data) — ở 4K đó là ~12 MB mỗi frame đi vòng GPU→CPU→GPU.
//   Thay vào đó:
//        VASurface ──vaExportSurfaceHandle──► dma-buf fd
//                  ──eglCreateImageKHR──────► EGLImage
//                  ──glEGLImageTargetTexture2DOES──► texture
//   Không byte nào rời VRAM. Đường lùi (giải mã bằng CPU) thì upload bằng
//   glTexSubImage2D như bình thường.
//
// ⚠ ĐỔI MÀU BT.709 TRONG SHADER
//   Ta nhận NV12/YUV420P và phải ra RGB. Hệ số trong shader là BT.709 DẢI HẸP
//   (limited range, 16-235) — phải KHỚP với colour_description mà VaEncoder ghi
//   vào SPS (encode/VaEncoder.cpp, BuildParameterSets). Đổi một bên mà quên bên
//   kia thì hình ám màu nhẹ, đủ để đổ oan cho màn hình chứ không đủ để thấy ngay.
//
// LIÊN QUAN: render/VideoSink.h (hợp đồng quyền sở hữu), decode/AvDecoder.h,
//            gtk/ViewerWindow.cpp (chủ sở hữu GtkGLArea)
// =============================================================================
#include <va/va.h>
#include <va/va_drmcommon.h>

#include <atomic>
#include <cstdint>
#include <mutex>

#include "render/VideoSink.h"

class VideoRenderer : public VideoSink {
public:
    VideoRenderer() = default;
    ~VideoRenderer() override;
    VideoRenderer(const VideoRenderer&) = delete;
    VideoRenderer& operator=(const VideoRenderer&) = delete;

    // --- Gọi từ thread Decode ---
    void SubmitFrame(void* avFrame, uint64_t ptsUs) override;
    void SetVaDisplay(void* vaDisplay) override;
    void DropFrames() override;

    // --- Gọi từ thread GTK, khi context OpenGL đang hiện hành ---
    bool Realize();
    void Unrealize();
    // Vẽ frame mới nhất vào framebuffer đang bind, giữ đúng tỉ lệ khung (viền đen
    // hai bên nếu cần). false = chưa có frame nào để vẽ.
    bool Render(int viewW, int viewH);

    // Tô đen toàn khung. Dùng khi chưa có frame nào — không tô thì thấy rác của
    // framebuffer lần trước. Nằm ở đây chứ không ở ViewerWindow để giữ nguyên
    // ranh giới "chỉ file này chạm OpenGL" (xem ⚠ ở đầu header).
    void ClearBlack();

    // Số frame đã vẽ kể từ lần gọi trước — nuôi số liệu fps của overlay.
    uint32_t TakeRenderedCount() override {
        return rendered_.exchange(0, std::memory_order_relaxed);
    }
    // PTS (đồng hồ host) của frame vừa vẽ gần nhất — mốc tính trễ e2e.
    // Khác VtDecoder bên macOS: ở đây mốc này là lúc VẼ THẬT (hàm Render chạy
    // xong), không phải lúc enqueue — nên con số e2e sát thực tế hơn bản macOS,
    // chỉ còn thiếu quãng compositor đưa lên tấm nền.
    uint64_t lastRenderedPtsUs() const override {
        return lastRenderedPts_.load(std::memory_order_relaxed);
    }
    // Có frame đang chờ vẽ không (UI dùng để biết khi nào bỏ màn hình chờ).
    bool hasFrame() const {
        return hasFrame_.load(std::memory_order_acquire);
    }

private:
    // Nhả frame đang giữ + đóng fd dma-buf. GỌI KHI ĐANG GIỮ mutex_.
    void ClearSlotLocked();
    // Dựng texture cho frame trong slot. GỌI TRÊN THREAD GTK, đang giữ mutex_.
    bool UploadLocked();

    // --- Khe frame, hai thread cùng chạm, bảo vệ bằng mutex_ ---
    std::mutex mutex_;
    void* frame_ = nullptr; // AVFrame*, sở hữu
    bool dmabuf_ = false;
    VADRMPRIMESurfaceDescriptor desc_{}; // chỉ có nghĩa khi dmabuf_
    uint64_t ptsUs_ = 0;
    std::atomic<bool> hasFrame_{false};

    // VADisplay của decoder. Chỉ thread Decode ghi, và chỉ thread Decode đọc.
    void* vaDisplay_ = nullptr;

    // --- Tài nguyên OpenGL, chỉ thread GTK chạm ---
    unsigned program_ = 0;
    unsigned vao_ = 0, vbo_ = 0;
    unsigned tex_[3] = {0, 0, 0};
    int uPlanarUv_ = -1;
    bool glReady_ = false;

    std::atomic<uint32_t> rendered_{0};
    std::atomic<uint64_t> lastRenderedPts_{0};
};
