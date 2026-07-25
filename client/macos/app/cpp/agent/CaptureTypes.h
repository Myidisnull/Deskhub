#pragma once
// =============================================================================
// CaptureTypes.h — kiểu dữ liệu ở ranh giới giữa tầng bắt hình và tầng tiêu thụ.
//                  Đối ứng client/windows/capture/CaptureTypes.h.
//
// NHIỆM VỤ
//   Định nghĩa CaptureTarget ("bắt cái gì") và MacFrameInfo ("một khung hình vừa
//   bắt được"). Đây là hợp đồng giữa ScreenCapture (bên sản xuất) và VtEncoder
//   (bên tiêu thụ), tách riêng để hai bên không phải include lẫn nhau.
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
// LIÊN QUAN: agent/ScreenCapture.h (bên sản xuất), agent/VtEncoder.h (bên tiêu thụ),
//            agent/SourceEnum.h (nguồn sinh ra CaptureTarget)
// =============================================================================
#include <cstdint>

// Nguồn cần bắt. Một trong hai vai: MỘT cửa sổ, hoặc CẢ một màn hình.
//
// Vì sao gộp vào một struct với cờ `isDisplay` thay vì hai trường như bản Windows
// (HWND/HMONITOR): CGWindowID và CGDirectDisplayID đều là uint32_t, không có kiểu
// riêng để phân biệt — nên phải có cờ tường minh, và có cờ rồi thì một trường id là
// đủ.
struct CaptureTarget {
    bool isDisplay = false;
    uint32_t id = 0; // CGWindowID (cửa sổ) hoặc CGDirectDisplayID (màn hình)

    bool valid() const {
        return id != 0;
    }
    static CaptureTarget Window(uint32_t windowId) {
        return CaptureTarget{false, windowId};
    }
    static CaptureTarget Display(uint32_t displayId) {
        return CaptureTarget{true, displayId};
    }
};

// Một frame vừa bắt được. `pixelBuffer` là CVPixelBufferRef (NV12, IOSurface-backed)
// và CHỈ hợp lệ trong phạm vi lời gọi callback — xem quy tắc vòng đời ở đầu file.
struct MacFrameInfo {
    void* pixelBuffer;    // CVPixelBufferRef
    uint32_t width;       // luôn chẵn
    uint32_t height;      // luôn chẵn
    uint64_t timestampUs; // NowUs() lúc frame tới, micro giây
};
