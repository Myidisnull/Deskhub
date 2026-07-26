// =============================================================================
// AppState.kt — hai tuỳ chọn toàn app: giao diện sáng/tối và ngôn ngữ EN/VI.
// Đối ứng client/ios/app/swift/AppState.swift và `APP` trong i18n.jsx.
//
// VÌ SAO LÀ MỘT OBJECT CHỨ KHÔNG PHẢI CompositionLocal
//   Đúng một cái tồn tại trong đời sống app, và cả bảng chữ (hàm tr() — không nằm
//   trong cây composable nào) lẫn thanh trên cùng đều cần tới. mutableStateOf lo phần
//   vẽ lại: composable nào ĐỌC AppState.lang — kể cả gián tiếp qua tr() — đều được
//   Compose ghi nhận và vẽ lại khi giá trị đổi.
//
// KHÔNG THEO GIAO DIỆN CỦA HỆ ĐIỀU HÀNH
//   Hệ thiết kế là DARK-ONLY về gốc; nền sáng là bản bổ sung. Người dùng mở app này
//   để nhìn màn hình một máy khác — nền tối là nền đúng cho việc đó bất kể điện thoại
//   đang để chế độ gì, nên mặc định là tối và chỉ đổi khi người dùng tự bấm.
//
// PHẢI GỌI init(context) TRƯỚC setContent — MainActivity làm việc đó. Object Kotlin
// không có Context để tự đọc SharedPreferences như Swift đọc UserDefaults được.
// =============================================================================
package com.deskhub.app.ui

import android.content.Context
import android.content.SharedPreferences
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

object AppState {
    private const val THEME_KEY = "appearance"
    private const val LANG_KEY = "language"

    private var prefs: SharedPreferences? = null

    var isDark: Boolean by mutableStateOf(true)
        private set

    /** "en" hoặc "vi". */
    var lang: String by mutableStateOf("en")
        private set

    /** Nạp giá trị đã lưu. Gọi lặp vô hại — chỉ lần đầu có tác dụng. */
    fun init(context: Context) {
        if (prefs != null) return
        val p = context.applicationContext.getSharedPreferences("deskhub", Context.MODE_PRIVATE)
        prefs = p
        isDark = p.getString(THEME_KEY, "dark") != "light"
        lang = if (p.getString(LANG_KEY, "en") == "vi") "vi" else "en"
    }

    fun toggleTheme() {
        isDark = !isDark
        prefs?.edit()?.putString(THEME_KEY, if (isDark) "dark" else "light")?.apply()
    }

    fun toggleLang() {
        lang = if (lang == "en") "vi" else "en"
        prefs?.edit()?.putString(LANG_KEY, lang)?.apply()
    }
}
