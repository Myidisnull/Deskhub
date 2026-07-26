// =============================================================================
// AppState.swift — hai tuỳ chọn toàn app: giao diện sáng/tối và ngôn ngữ EN/VI.
// Đối ứng client/macos/app/swift/AppState.swift và `APP` trong i18n.jsx.
//
// VÌ SAO LÀ MỘT SINGLETON CHỨ KHÔNG PHẢI @Environment
//   Đúng một cái tồn tại trong đời sống app, và cả bảng chữ (Strings.swift) lẫn thanh
//   trên cùng đều cần tới. Bảng chữ là hàm thường `tr("key")` — nó không nằm trong cây
//   view nên không có Environment nào tới được nó. Nhét ngôn ngữ vào Environment rồi
//   truyền xuống từng chỗ gọi tr() chỉ thêm một tham số lặp lại ở mọi dòng chữ.
//
//   @Observable lo phần vẽ lại: view nào ĐỌC AppState.shared.lang trong body — kể cả
//   gián tiếp qua tr() — đều được Observation ghi nhận và vẽ lại khi giá trị đổi.
//
// GIAO DIỆN SÁNG/TỐI KHÔNG TỰ VIẾT BỘ ĐỔI MÀU
//   ContentView đặt .preferredColorScheme theo cờ này; mọi màu động trong
//   DesignTokens.swift tự phân giải lại theo traitCollection. Xem ghi chú đầu file đó.
//
// KHÔNG THEO GIAO DIỆN CỦA HỆ ĐIỀU HÀNH
//   Hệ thiết kế là DARK-ONLY về gốc; nền sáng là bản bổ sung. Người dùng mở app này
//   để nhìn màn hình một máy khác — nền tối là nền đúng cho việc đó bất kể điện thoại
//   đang để chế độ gì, nên mặc định là tối và chỉ đổi khi người dùng tự bấm.
// =============================================================================
import Observation
import SwiftUI

@MainActor @Observable
final class AppState {
    static let shared = AppState()

    private static let themeKey = "appearance"
    private static let langKey = "language"

    var isDark: Bool {
        didSet { UserDefaults.standard.set(isDark ? "dark" : "light", forKey: Self.themeKey) }
    }

    // "en" hoặc "vi".
    var lang: String {
        didSet { UserDefaults.standard.set(lang, forKey: Self.langKey) }
    }

    private init() {
        isDark = UserDefaults.standard.string(forKey: Self.themeKey) != "light"
        lang = UserDefaults.standard.string(forKey: Self.langKey) == "vi" ? "vi" : "en"
    }

    func toggleTheme() { isDark.toggle() }
    func toggleLang() { lang = lang == "en" ? "vi" : "en" }

    var colorScheme: ColorScheme { isDark ? .dark : .light }
}
