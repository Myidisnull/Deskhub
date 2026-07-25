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
//   Accessibility    — chỉ cần khi cho phép điều khiển (opt.allowInput). Chia sẻ
//                      kiểu chỉ-xem vẫn chạy đủ mà không cần quyền này.
//
// HÀNH VI CỦA HAI HÀM Request*
//   Chúng bật hộp thoại của hệ thống (một lần duy nhất cho mỗi app) rồi TRẢ VỀ NGAY
//   — người dùng phải tự vào System Settings bật công tắc. macOS đòi khởi động lại
//   app sau khi cấp quyền Screen Recording, nên UI phải nói rõ điều đó thay vì chờ
//   một giá trị true không bao giờ tới.
//
// LIÊN QUAN: agent/SourceEnum.h, agent/InputInjector.h (hai nơi chịu hậu quả),
//            docs/14-macos-app.md §5
// =============================================================================

namespace macperm {

// true nếu app đã được cấp quyền Screen Recording (CGPreflightScreenCaptureAccess).
bool HasScreenRecording();

// Bật hộp thoại xin quyền Screen Recording. Trả về giá trị preflight NGAY sau lời
// gọi — gần như luôn false ở lần đầu, vì người dùng còn phải bật công tắc và khởi
// động lại app.
bool RequestScreenRecording();

// true nếu app nằm trong danh sách Accessibility (AXIsProcessTrusted).
bool HasAccessibility();

// Bật hộp thoại xin quyền Accessibility (kAXTrustedCheckOptionPrompt). Khác Screen
// Recording ở một điểm dễ chịu: quyền này có hiệu lực NGAY, không phải khởi động
// lại app.
bool RequestAccessibility();

// Mở đúng trang trong System Settings để người dùng khỏi phải mò.
void OpenScreenRecordingSettings();
void OpenAccessibilitySettings();

} // namespace macperm
