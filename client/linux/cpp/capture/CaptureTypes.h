#pragma once
// =============================================================================
// CaptureTypes.h — kiểu dữ liệu ở ranh giới giữa tầng bắt hình và tầng tiêu thụ.
//                  Đối ứng client/macos/.../capture/CaptureTypes.h và
//                  client/windows/cpp/capture/CaptureTypes.h.
//
// NHIỆM VỤ
//   Định nghĩa LinuxFrameInfo ("một khung hình vừa bắt được"). Đây là hợp đồng giữa
//   ScreenCapture (bên sản xuất, PipeWire) và VaEncoder (bên tiêu thụ, VA-API),
//   tách riêng để hai bên không phải include lẫn nhau.
//
// VÌ SAO CỐ Ý KHÔNG PHỤ THUỘC PIPEWIRE
//   pipewire/pipewire.h và spa/* kéo theo một bộ header rất lớn và một mô hình
//   vòng đời riêng. Encoder không cần biết frame đến từ đâu — nó chỉ cần con trỏ
//   dữ liệu hoặc một nắm fd dma-buf. Giữ file này ở mức C++ thuần nghĩa là
//   AgentLoop.cpp biên dịch được như C++ thường, và sau này đổi nguồn bắt hình
//   (X11/XShm cho phiên Xorg, hay wlr-screencopy) thì bên tiêu thụ không phải sửa gì.
//
// ⚠ HAI ĐƯỜNG BỘ NHỚ — ĐÂY LÀ KHÁC BIỆT LỚN NHẤT SO VỚI WINDOWS/macOS
//   Trên Windows frame LUÔN là texture D3D11 trong VRAM; trên macOS LUÔN là
//   CVPixelBuffer có IOSurface. Trên Linux, PipeWire thoả thuận với compositor
//   xem dùng đường nào, và ta phải chịu cả hai:
//
//     DmaBuf  — ĐƯỜNG NHANH, và là đường mọi compositor hiện đại chọn. Frame nằm
//               nguyên trong VRAM; ta chỉ nhận vài fd + offset/stride/modifier rồi
//               import thẳng vào VA-API (VADRMPRIMESurfaceDescriptor). KHÔNG có
//               byte nào qua CPU — đúng tinh thần "hot path never touches the CPU"
//               của dự án.
//     Mapped  — ĐƯỜNG LÙI, khi compositor hoặc driver không thoả thuận được
//               modifier. Frame đã nằm trong RAM và ta phải chép nó lên GPU
//               (vaPutImage). Ở 4K, một frame BGRA là ~33 MB → 60 fps là ~2 GB/s
//               băng thông bộ nhớ, tức là thực tế sẽ tụt xuống ~20-30 fps. Chấp
//               nhận được vì nó chỉ là lối thoát hiểm, nhưng phải BIẾT là mình
//               đang ở đó — VaEncoder log rõ đường nào đang dùng.
//
// ⚠ QUY TẮC VÒNG ĐỜI QUAN TRỌNG NHẤT
//   Mọi thứ trong LinuxFrameInfo — con trỏ `data` lẫn các fd dma-buf — CHỈ hợp lệ
//   trong phạm vi lời gọi callback. Buffer thuộc về pool của PipeWire và được trả
//   lại (pw_stream_queue_buffer) ngay khi callback trả về. Bên tiêu thụ phải encode
//   NGAY, hoặc tự dup() fd nếu muốn giữ lại. Giữ con trỏ/fd trần để dùng sau là đọc
//   phải dữ liệu của một khung hình khác — lỗi không gây crash, chỉ cho ra hình
//   sai, nên rất khó lần ra nếu không biết trước quy tắc này.
//
// KÍCH THƯỚC LUÔN CHẴN
//   ScreenCapture làm tròn XUỐNG số chẵn trước khi báo ra. Lý do: H.264 lấy mẫu
//   chroma theo khối 2×2, và bộ mã hoá VA-API từ chối kích thước lẻ. Giống hệt lý
//   do của bản macOS.
//
// LIÊN QUAN: capture/ScreenCapture.h (bên sản xuất), encode/VaEncoder.h (bên tiêu
//            thụ), capture/PortalScreenCast.h (nơi lấy nodeId)
// =============================================================================
#include <cstdint>

// Frame nằm ở đâu — xem "hai đường bộ nhớ" ở đầu file.
enum class FrameMemory : uint8_t {
    Mapped, // RAM, đã map sẵn; đọc qua `data` + `stride`
    DmaBuf, // VRAM; import qua `planes` + `modifier`
};

// Số mặt phẳng tối đa của một dma-buf. Định dạng ta thoả thuận (BGRx/BGRA/NV12)
// nhiều nhất là 2 mặt; để 4 cho khớp giới hạn của DRM và của SPA.
inline constexpr uint32_t kMaxDmaPlanes = 4;

struct DmaPlane {
    int fd = -1;         // KHÔNG sở hữu — thuộc buffer của PipeWire
    uint32_t offset = 0; // byte tính từ đầu fd
    uint32_t stride = 0;
};

// Một frame vừa bắt được. Xem quy tắc vòng đời ở đầu file.
struct LinuxFrameInfo {
    FrameMemory memory = FrameMemory::Mapped;

    // --- Nhánh Mapped ---
    const uint8_t* data = nullptr;
    uint32_t stride = 0;

    // --- Nhánh DmaBuf ---
    DmaPlane planes[kMaxDmaPlanes]{};
    uint32_t planeCount = 0;
    uint64_t modifier = 0; // DRM format modifier đã thoả thuận

    // --- Chung ---
    // Mã fourcc của DRM (drm_fourcc.h): DRM_FORMAT_XRGB8888, ARGB8888, XBGR8888,
    // ABGR8888, NV12. Dùng fourcc của DRM chứ không phải enum riêng vì cả hai đầu
    // tiêu thụ (VA-API import dma-buf và EGL_LINUX_DMA_BUF_EXT) đều nói bằng nó —
    // dịch qua một enum trung gian chỉ tạo thêm một bảng để lệch nhau.
    uint32_t drmFormat = 0;
    uint32_t width = 0;  // luôn chẵn
    uint32_t height = 0; // luôn chẵn
    uint64_t timestampUs = 0;
};
