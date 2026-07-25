#pragma once
// =============================================================================
// SourceEnum.h — liệt kê những gì máy này chia sẻ được: cửa sổ đang mở + màn hình.
//                Đối ứng client/windows/capture/WindowFinder.h.
//
// NHIỆM VỤ
//   Trả lời câu hỏi của màn hình "Share": người dùng chọn chia sẻ cái gì? Kết quả
//   là danh sách CaptureTarget kèm nhãn để hiện lên UI.
//
// VÌ SAO DÙNG SCShareableContent CHỨ KHÔNG PHẢI CGWindowListCopyWindowInfo
//   CGWindowList vẫn chạy và không cần quyền, nhưng nó liệt kê cả những cửa sổ mà
//   ScreenCaptureKit từ chối bắt (cửa sổ hệ thống, lớp phủ, cửa sổ của chính app).
//   Người dùng chọn phải một cái như thế thì phiên chết ngay ở frame đầu mà không
//   có lý do rõ ràng. Hỏi thẳng SCShareableContent cho ta ĐÚNG tập bắt được — cùng
//   nguồn sự thật với ScreenCapture, nên không thể lệch nhau.
//
// ⚠ CHẶN, VÀ CẦN QUYỀN
//   - GetSources CHẶN tới ~2 giây (API bất đồng bộ, ta chờ bằng semaphore) → gọi
//     ngoài main thread, giống QuerySources của vai client.
//   - Thiếu quyền Screen Recording, macOS KHÔNG báo lỗi mà chỉ trả về cửa sổ của
//     chính app này. Caller phải hỏi macperm::HasScreenRecording() trước và nói cho
//     người dùng, chứ đừng hiện một danh sách trống khó hiểu — xem Permissions.h.
//
// LỌC SẴN NHỮNG THỨ CHẮC CHẮN VÔ DỤNG
//   Bỏ cửa sổ của chính Deskhub (chia sẻ chính mình = gương hồi quy vô tận), cửa sổ
//   không tiêu đề và cửa sổ nhỏ hơn kMinSize — xem .mm về lý do từng mục.
//
// LIÊN QUAN: agent/CaptureTypes.h (CaptureTarget), agent/ScreenCapture.h (bên tiêu
//            thụ), agent/Permissions.h, client/windows/capture/WindowFinder.h
// =============================================================================
#include <string>
#include <vector>

#include "agent/CaptureTypes.h"

// Một mục trong danh sách chọn nguồn.
struct ShareSource {
    CaptureTarget target;
    std::string name;   // "Safari — Deskhub on GitHub" / "Display 1 (3456×2234)"
    uint32_t width = 0; // kích thước hiện tại, PIXEL (đã nhân scale màn hình)
    uint32_t height = 0;
};

// CHẶN tới ~2 giây. Trả danh sách rỗng nếu thiếu quyền hoặc không có gì chia sẻ được.
// Màn hình đứng TRƯỚC cửa sổ: "chia sẻ cả màn hình" là lựa chọn hay dùng nhất.
std::vector<ShareSource> GetShareSources();
