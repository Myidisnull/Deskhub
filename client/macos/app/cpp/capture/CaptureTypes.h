#pragma once
// =============================================================================
// CaptureTypes.h — kiểu dữ liệu ở ranh giới giữa tầng bắt hình và tầng tiêu thụ.
//                  Đối ứng client/windows/capture/CaptureTypes.h.
//
// NHIỆM VỤ
//   Định nghĩa MacFrameInfo ("một khung hình vừa bắt được"). Đây là hợp đồng giữa
//   ScreenCapture (bên sản xuất) và VtEncoder (bên tiêu thụ), tách riêng để hai
//   bên không phải include lẫn nhau. Nguồn bắt là MỘT MÀN HÌNH, định danh bằng
//   CGDirectDisplayID trần (share theo cửa sổ đã bỏ 2026-07-27).
//
// VÌ SAO CỐ Ý KHÔNG PHỤ THUỘC ScreenCaptureKit
//   SCStream/SCWindow là API Objective-C, kéo theo cả một bộ header nặng và buộc
//   file tiêu thụ phải là .mm. Encoder không cần biết frame đến từ đâu — nó chỉ cần
//   một CVPixelBuffer. Giữ file này ở mức C++ thuần + void* nghĩa là AgentLoop.cpp
//   biên dịch được như C++ thường, và sau này đổi sang nguồn bắt hình khác
//   (CGDisplayStream, AVCaptureScreenInput) thì bên tiêu thụ không phải sửa gì.
//
// ⚠ QUY TẮC VÒNG ĐỜI QUAN TRỌNG NHẤT
//   `pixelBuffer` CHỈ hợp lệ trong phạm vi lời gọi callback. Nó thuộc về pool của
//   SCStream và sẽ được tái sử dụng ngay khi callback trả về. Bên tiêu thụ phải
//   encode hoặc CVPixelBufferRetain NGAY; giữ con trỏ trần để dùng sau là đọc phải
//   dữ liệu của một khung hình khác — lỗi không gây crash, chỉ cho ra hình sai, nên
//   rất khó lần ra nếu không biết trước quy tắc này.
//
// KÍCH THƯỚC LUÔN CHẴN
//   ScreenCapture tự làm tròn XUỐNG số chẵn trước khi cấu hình SCStream, nên
//   width/height ở đây luôn chia hết cho 2. Lý do: H.264 lấy mẫu chroma theo khối
//   2×2, VideoToolbox từ chối kích thước lẻ. Bản Windows phải mang thêm cặp
//   srcWidth/srcHeight để video processor cắt cột lẻ dư; ở đây không cần, vì ta
//   quyết định kích thước buffer ngay từ lúc cấu hình stream chứ không nhận lấy
//   texture có sẵn.
//
// LIÊN QUAN: capture/ScreenCapture.h (bên sản xuất), encode/VtEncoder.h (bên tiêu thụ),
//            capture/SourceEnum.h (nơi liệt kê displayId)
// =============================================================================
#include <cstdint>

// Một frame vừa bắt được. `pixelBuffer` là CVPixelBufferRef (NV12, IOSurface-backed)
// và CHỈ hợp lệ trong phạm vi lời gọi callback — xem quy tắc vòng đời ở đầu file.
struct MacFrameInfo {
    void* pixelBuffer;    // CVPixelBufferRef
    uint32_t width;       // luôn chẵn
    uint32_t height;      // luôn chẵn
    uint64_t timestampUs; // NowUs() lúc frame tới, micro giây
};
