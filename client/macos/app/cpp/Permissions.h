#pragma once
// =============================================================================
// Permissions.h — hai quyền hệ thống mà vai HOST không thể chạy nếu thiếu.
//
// VÌ SAO PHẢI CÓ HẲN MỘT FILE CHO CHUYỆN NÀY
//   Trên macOS, thiếu quyền KHÔNG báo lỗi rõ ràng — nó im lặng cho ra kết quả sai:
//     - Thiếu Screen Recording: SCShareableContent chỉ trả về cửa sổ CỦA CHÍNH APP
//       này. Người dùng thấy danh sách nguồn gần như trống và tưởng máy hỏng.
//     - Thiếu Accessibility: CGEventPost chạy "thành công" (không mã lỗi) nhưng
//       không sự kiện nào tới ứng dụng đích. Triệu chứng "gõ không ăn" nhìn y hệt
//       lỗi mạng — cùng một bẫy mà UIPI gây ra bên Windows (ElevatedShare.h).
//   Nên phải hỏi TRƯỚC và nói thẳng cho người dùng, thay vì để họ tự đoán.
//
// KHI NÀO CẦN CÁI NÀO
//   Screen Recording — bắt buộc để chia sẻ (liệt kê nguồn + bắt hình).
//   Accessibility    — cũng bắt buộc từ 2026-07-27: chuột/bàn phím LUÔN được chia
//                      sẻ, không còn kiểu chia sẻ chỉ-xem để né quyền này.
//
// KHÔNG có hàm Request* (bỏ 2026-07-27): hộp thoại xin quyền của hệ chỉ hiện đúng
// một lần trong đời app rồi thôi, nên đường tin cậy duy nhất là mở thẳng trang
// System Settings — UI chỉ cần Has* + Open*Settings.
//
// LIÊN QUAN: capture/SourceEnum.h, input/InputInjector.h (hai nơi chịu hậu quả),
//            docs/14-macos-app.md §5
// =============================================================================

namespace macperm {

// true nếu app đã được cấp quyền Screen Recording (CGPreflightScreenCaptureAccess).
bool HasScreenRecording();

// true nếu app nằm trong danh sách Accessibility (AXIsProcessTrusted).
bool HasAccessibility();

// Mở đúng trang trong System Settings để người dùng khỏi phải mò.
void OpenScreenRecordingSettings();
void OpenAccessibilitySettings();

} // namespace macperm
