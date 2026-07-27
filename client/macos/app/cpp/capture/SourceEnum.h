#pragma once
// =============================================================================
// SourceEnum.h — liệt kê các MÀN HÌNH máy này chia sẻ được.
//                Đối ứng client/windows/capture/DisplayFinder.h.
//
// NHIỆM VỤ
//   Trả lời câu hỏi của màn hình "Share": chia sẻ màn hình nào (máy nhiều
//   monitor)? Kết quả là danh sách displayId kèm nhãn để hiện lên UI. (Danh sách
//   từng gồm cả cửa sổ đang mở — share theo cửa sổ đã bỏ 2026-07-27.)
//
// VÌ SAO DÙNG SCShareableContent CHỨ KHÔNG PHẢI CGGetActiveDisplayList
//   Hỏi thẳng SCShareableContent cho ta ĐÚNG tập ScreenCaptureKit bắt được — cùng
//   nguồn sự thật với ScreenCapture, nên không thể lệch nhau.
//
// ⚠ CHẶN, VÀ CẦN QUYỀN
//   - GetSources CHẶN tới ~2 giây (API bất đồng bộ, ta chờ bằng semaphore) → gọi
//     ngoài main thread, giống QuerySources của vai client.
//   - Thiếu quyền Screen Recording, macOS KHÔNG báo lỗi rõ ràng. Caller phải hỏi
//     macperm::HasScreenRecording() trước và nói cho người dùng, chứ đừng hiện một
//     danh sách trống khó hiểu — xem Permissions.h.
//
// LIÊN QUAN: capture/ScreenCapture.h (bên tiêu thụ), Permissions.h,
//            client/windows/capture/DisplayFinder.h
// =============================================================================
#include <cstdint>
#include <string>
#include <vector>

// Một màn hình trong danh sách chọn nguồn.
struct ShareSource {
    uint32_t displayId = 0; // CGDirectDisplayID
    std::string name;       // "Display 1 (3456×2234)"
    uint32_t width = 0;     // kích thước hiện tại, PIXEL
    uint32_t height = 0;
};

// CHẶN tới ~2 giây. Trả danh sách rỗng nếu thiếu quyền hoặc không có màn hình nào.
std::vector<ShareSource> GetShareSources();
