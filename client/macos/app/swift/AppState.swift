// =============================================================================
// AppState.swift — hai tuỳ chọn toàn app mà bản thiết kế đặt ở CHÂN THANH RAIL,
// cạnh nhau: giao diện sáng/tối và ngôn ngữ EN/VI.
// Đối ứng `APP` trong i18n.jsx của dự án thiết kế.
//
// VÌ SAO LÀ MỘT SINGLETON CHỨ KHÔNG PHẢI @Environment
//   Đúng một cái tồn tại trong đời sống app, và cả bảng chữ (Strings.swift) lẫn thanh
//   rail đều cần tới. Bảng chữ là hàm thường `tr("key")` — nó không nằm trong cây view
//   nên không có Environment nào tới được nó. Nhét ngôn ngữ vào Environment rồi truyền
//   xuống từng chỗ gọi tr() chỉ thêm một tham số lặp lại ở mọi dòng chữ trong app.
//
//   @Observable lo phần vẽ lại: view nào ĐỌC AppState.shared.lang trong body — kể cả
//   gián tiếp qua tr() — đều được Observation ghi nhận và vẽ lại khi giá trị đổi.
//
// GIAO DIỆN SÁNG/TỐI KHÔNG TỰ VIẾT BỘ ĐỔI MÀU
//   ContentView đặt .preferredColorScheme theo cờ này; mọi màu động trong
//   DesignTokens.swift tự phân giải lại theo NSAppearance. Xem ghi chú đầu file đó.
// =============================================================================
import Observation
import SwiftUI

@MainActor @Observable
final class AppState {
    static let shared = AppState()

    private static let themeKey = "appearance"
    private static let langKey = "language"

    // Hệ thiết kế là DARK-ONLY về gốc; nền sáng là bản bổ sung. Mặc định theo bản gốc.
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
