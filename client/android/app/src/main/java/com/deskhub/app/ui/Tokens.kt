// =============================================================================
// Tokens.kt — bảng token của hệ thiết kế Deskhub cho Android.
//
// CÙNG MỘT BỘ SỐ VỚI macOS, iOS VÀ WINDOWS
//   client/macos/app/swift/DesignTokens.swift, client/ios/app/swift/DesignTokens.swift
//   và client/windows/csharp/Themes/Tokens.xaml đã dịch bộ token này từ _ds/tokens/
//   *.css. File này lấy lại CHÍNH những giá trị màu ấy chứ không dịch lại lần nữa:
//   bốn nền tảng phải trông cùng một sản phẩm, và một chênh lệch 1% alpha ở đây sẽ
//   không ai phát hiện ra cho tới lúc đặt điện thoại cạnh màn hình máy tính.
//
// KÍCH THƯỚC THÌ KHÔNG CHÉP — chép y bản mobile của iOS (gutter 20, control 44,
//   field 58). Hai nền tảng cảm ứng có cùng ràng buộc công thái học và ngưỡng vùng
//   chạm gần như nhau (44pt của Apple, 48dp của Material), nên chúng dùng chung bộ
//   số; chỗ lệch duy nhất so với desktop đã được giải thích ở bản iOS.
//
// VÌ SAO KHÔNG DÙNG MaterialTheme.colorScheme
//   Material 3 sinh cả bảng màu từ một màu gốc theo luật riêng của nó (tone palette,
//   surface tint, elevation overlay). Hệ thiết kế này chỉ định TỪNG giá trị một —
//   kính trắng 4.5%, viền tóc 9%, mint #66e3b8 — nên đưa chúng vào colorScheme sẽ bị
//   Material trộn lại rồi vẽ ra một thứ khác. Ta cấp màu qua CompositionLocal riêng
//   và không thành phần nào bên dưới đọc colorScheme nữa.
//
// VÌ SAO CompositionLocal CHỨ KHÔNG PHẢI HAI HẰNG SỐ TRA THẲNG
//   Compose không có "màu động theo cấu hình" như UIColor/NSColor. Bảng màu phải đi
//   xuống cây theo một đường nào đó, và CompositionLocal là đường ngắn nhất: đổi
//   AppState.isDark là mọi thứ dưới DeskhubTheme vẽ lại, không view nào phải nhận
//   thêm một tham số "đang sáng hay tối".
//
// LIÊN QUAN: Components.kt (thành phần dựng từ bảng này), AppState.kt (cờ sáng/tối)
// =============================================================================
package com.deskhub.app.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp

/**
 * Bảng màu của một giao diện (tối hoặc sáng).
 *
 * NỀN SÁNG ĐẢO CHIỀU LỚP PHỦ, KHÔNG PHẢI ĐẢO ĐỘ ĐẬM CỦA CÙNG MỘT LỚP: ở nền tối mọi
 * bề mặt là một lớp TRẮNG mờ chồng lên nền gần-đen; ở nền sáng bề mặt trở thành
 * TRẮNG ĐẶC còn nền lùi xuống xám (#eef1f6) — thẻ NỔI LÊN khỏi nền chứ không chìm vào.
 */
@Immutable
data class DsColors(
    val bgWindow: Color,
    val surfacePanel: Color,
    val surfaceField: Color,
    val surfaceCard: Color,
    val surfaceCardPress: Color,
    val surfaceControl: Color,
    val surfaceControlPress: Color,
    val borderHairline: Color,
    val bgChrome: Color,
    val textPrimary: Color,
    val textSecondary: Color,
    val textOnAccent: Color,
    val accent: Color,
    val accentPress: Color,
    val accentWash: Color,
    val accentDisabled: Color,
    val borderActive: Color,
    val statusIdle: Color,
    val statusError: Color,
    val dangerWash: Color,
    val dangerLine: Color,
    val checkboxOff: Color,
    val glowCobalt: Color,
)

private val DarkColors =
    DsColors(
        bgWindow = Color(0xFF07090C),
        surfacePanel = Color(0xFF0D1116),
        surfaceField = Color(0xFF121820),
        surfaceCard = Color(0x0BFFFFFF), // trắng 4.5%
        surfaceCardPress = Color(0x17FFFFFF), // 9%
        surfaceControl = Color(0x0FFFFFFF), // 6%
        surfaceControlPress = Color(0x1AFFFFFF), // 10.2%
        borderHairline = Color(0x17FFFFFF), // 9%
        bgChrome = Color(0x9E0A0E12), // 62%
        textPrimary = Color(0xFFF2F5F7),
        textSecondary = Color(0xFF7D8892),
        textOnAccent = Color(0xFF04120C),
        accent = Color(0xFF66E3B8),
        accentPress = Color(0xFF2BB98A),
        accentWash = Color(0x2466E3B8), // 14%
        accentDisabled = Color(0x14FFFFFF), // 8%
        borderActive = Color(0x5966E3B8), // 35%
        statusIdle = Color(0xFF7D8892),
        statusError = Color(0xFFFF6B61),
        dangerWash = Color(0x24FF453A), // 14%
        dangerLine = Color(0x52FF453A), // 32%
        // Ô tick chưa chọn ở nền tối là một hõm ĐEN — phải tối hơn panel để trông như
        // chỗ lõm xuống chờ được đánh dấu.
        checkboxOff = Color(0x59000000), // đen 35%
        glowCobalt = Color(0x8C1B3FA0), // 55%
    )

private val LightColors =
    DsColors(
        bgWindow = Color(0xFFEEF1F6),
        surfacePanel = Color(0xFFFFFFFF),
        surfaceField = Color(0xFFFFFFFF),
        surfaceCard = Color(0xEBFFFFFF), // 92%
        surfaceCardPress = Color(0xFFF1F4F9),
        surfaceControl = Color(0xFFFFFFFF),
        surfaceControlPress = Color(0xFFE4E9F1),
        borderHairline = Color(0x290E1A2A), // 16%
        bgChrome = Color(0xE0FFFFFF), // 88%
        textPrimary = Color(0xFF0A1017),
        textSecondary = Color(0xFF4A5663),
        textOnAccent = Color(0xFFFFFFFF),
        accent = Color(0xFF17A97C),
        accentPress = Color(0xFF0D7D5B),
        accentWash = Color(0x1F17A97C), // 12%
        accentDisabled = Color(0x1A0E1A2A), // 10%
        borderActive = Color(0x6617A97C), // 40%
        statusIdle = Color(0xFF5B6774),
        statusError = Color(0xFFD13A2F),
        dangerWash = Color(0x1FD13A2F), // 12%
        dangerLine = Color(0x52D13A2F), // 32%
        // Ở nền sáng, một hõm tối sẽ đọc thành ĐÃ tick — nên nó là ô trắng và dựa vào
        // viền tóc để vẫn thấy được.
        checkboxOff = Color(0xFFFFFFFF),
        glowCobalt = Color(0x2E1B3FA0), // 18%
    )

val LocalDsColors = staticCompositionLocalOf { DarkColors }

/** Vỏ giao diện: cấp bảng màu đúng theo cờ sáng/tối cho cả cây bên dưới. */
@Composable
fun DeskhubTheme(
    dark: Boolean,
    content: @Composable () -> Unit,
) {
    CompositionLocalProvider(LocalDsColors provides if (dark) DarkColors else LightColors, content = content)
}

/**
 * Token dùng ở mọi nơi: `Ds.colors.accent`, `Ds.radiusLg`, `Ds.textBodyLg`.
 * Tên trùng với bản Swift để tra chéo hai nền tảng được bằng mắt.
 */
object Ds {
    val colors: DsColors
        @Composable @ReadOnlyComposable
        get() = LocalDsColors.current

    // --- Bo góc: không gì sắc, không gì tròn hẳn trừ pill ---
    val radiusSm = 10.dp
    val radiusMd = 12.dp
    val radiusLg = 16.dp
    val radiusXl = 18.dp
    val radiusPill = 999.dp

    // --- Kích thước bố cục (chép bản iOS, xem ghi chú đầu file) ---
    val controlHeightSm = 34.dp
    val controlHeight = 44.dp
    val controlHeightLg = 52.dp
    val iconButton = 44.dp
    val fieldHeight = 58.dp
    val screenGutter = 20.dp
    val hairline = 1.dp

    // --- Chữ ---
    val textTitle = 28.sp
    val textSubtitle = 20.sp
    val textStat = 28.sp
    val textBodyLg = 16.sp
    val textBody = 15.sp
    val textBodySm = 13.sp
    val textMono = 14.sp
    val textMonoSm = 12.sp
    val textLabel = 10.sp

    // Giãn chữ: CSS ghi bằng em, Compose nhận thẳng em — khỏi quy đổi như bên SwiftUI.
    val trackDisplay = (-0.035).em
    val trackTitle = (-0.03).em
    val trackLabel = 0.2.em

    // Phông hiển thị (tiêu đề, số to) và phông mono (MỌI con số sản phẩm hiện ra).
    // Bản thiết kế dùng Space Grotesk + SF Mono; không nền tảng nào trong bốn nền
    // tảng kèm được cả hai mà không đóng gói phông, nên tất cả cùng dùng chuỗi dự
    // phòng của chính bản thiết kế: phông hệ thống cho hiển thị, mono hệ thống cho số.
    val display = FontFamily.Default
    val mono = FontFamily.Monospace

    // --- Chuyển động: nhanh, im lặng, chỉ đổi màu/viền. Không gì phóng to, không nảy.
    const val EASE_MS = 140
}
