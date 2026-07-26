// =============================================================================
// Icons.kt — năm icon của app, vẽ bằng Canvas theo đúng hình dáng SF Symbols mà
// bản iOS dùng (sun.max, moon, keyboard, terminal, macwindow).
//
// VÌ SAO VẼ TAY CHỨ KHÔNG DÙNG KÝ TỰ VĂN BẢN HAY BỘ ICON
//   Ký tự văn bản (☼ ☾ ⌨) là chữ: kích thước và vị trí do FONT HỆ THỐNG quyết định —
//   mỗi máy một font, glyph nhỏ hơn khung chữ, baseline không trùng tâm ô — nên icon
//   bị bé và lệch so với SF Symbols bên iOS. material-icons-extended thì nặng cả MB
//   cho đúng năm hình. Vẽ Canvas cho ta điều khiển từng dp: nét 1.5dp bo tròn đầu —
//   trùng độ đậm của SF Symbol cỡ regular — và hình luôn nằm chính giữa khung.
//
// MỖI ICON NHẬN size + color, KHÔNG NHẬN Modifier: chúng chỉ được dùng bên trong
// DsIconButton/HeroField — nơi đã lo nền, viền và căn giữa.
//
// LIÊN QUAN: Components.kt (TopBar, HeroField, SourceRow), StreamActivity (HUD)
// =============================================================================
package com.deskhub.app.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathOperation
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlin.math.cos
import kotlin.math.sin

// Nét chuẩn của cả bộ: 1.5dp đầu tròn — trùng SF Symbol cỡ regular ở 17pt.
private const val STROKE_DP = 1.5f

/** sun.max — vòng tròn giữa + 8 tia. Nút đổi sang giao diện SÁNG. */
@Composable
fun SunIcon(
    size: Dp = 20.dp,
    color: Color,
) {
    Canvas(modifier = Modifier.size(size)) {
        val stroke = STROKE_DP.dp.toPx()
        val c = center
        val dim = this.size.minDimension
        drawCircle(
            color = color,
            radius = dim * 0.22f,
            center = c,
            style = Stroke(width = stroke),
        )
        // 8 tia quanh vòng: bắt đầu từ 0° cho tia nằm ngang/dọc/chéo như sun.max.
        val inner = dim * 0.35f
        val outer = dim * 0.48f
        for (i in 0 until 8) {
            val angle = i * (Math.PI / 4).toFloat()
            val dir = Offset(cos(angle), sin(angle))
            drawLine(
                color = color,
                start = c + dir * inner,
                end = c + dir * outer,
                strokeWidth = stroke,
                cap = StrokeCap.Round,
            )
        }
    }
}

/** moon — trăng khuyết đặc, khuyết về phía trên-phải. Nút đổi sang giao diện TỐI. */
@Composable
fun MoonIcon(
    size: Dp = 20.dp,
    color: Color,
) {
    Canvas(modifier = Modifier.size(size)) {
        val dim = this.size.minDimension
        val r = dim * 0.36f
        val full =
            Path().apply {
                addOval(Rect(center = center, radius = r))
            }
        // "Miếng cắn" là một hình tròn to hơn lệch về trên-phải; phần hiệu của hai
        // hình chính là lưỡi liềm — cùng cách SF Symbols dựng hình moon.
        val bite =
            Path().apply {
                addOval(
                    Rect(
                        center = center + Offset(r * 0.62f, -r * 0.4f),
                        radius = r * 0.92f,
                    ),
                )
            }
        val crescent =
            Path().apply {
                op(full, bite, PathOperation.Difference)
            }
        drawPath(path = crescent, color = color)
    }
}

/** keyboard — khung bo góc + hai hàng phím chấm + thanh space. Nút bàn phím ảo. */
@Composable
fun KeyboardIcon(
    size: Dp = 18.dp,
    color: Color,
) {
    Canvas(modifier = Modifier.size(size)) {
        val stroke = STROKE_DP.dp.toPx()
        val w = this.size.width
        val h = w * 0.68f
        val top = (this.size.height - h) / 2f
        drawRoundRect(
            color = color,
            topLeft = Offset(0f + stroke / 2, top + stroke / 2),
            size = Size(w - stroke, h - stroke),
            cornerRadius = CornerRadius(w * 0.14f),
            style = Stroke(width = stroke),
        )
        // Hai hàng ba "phím": chấm vuông nhỏ, cùng cữ với keyboard của SF Symbols.
        val key = w * 0.075f
        val rowYs = listOf(top + h * 0.3f, top + h * 0.52f)
        val colXs = listOf(w * 0.26f, w * 0.5f, w * 0.74f)
        for (rowY in rowYs) {
            for (colX in colXs) {
                drawRoundRect(
                    color = color,
                    topLeft = Offset(colX - key / 2, rowY - key / 2),
                    size = Size(key, key),
                    cornerRadius = CornerRadius(key * 0.3f),
                )
            }
        }
        // Thanh space.
        drawLine(
            color = color,
            start = Offset(w * 0.32f, top + h * 0.74f),
            end = Offset(w * 0.68f, top + h * 0.74f),
            strokeWidth = stroke,
            cap = StrokeCap.Round,
        )
    }
}

/** terminal — khung bo góc + dấu nhắc `>_`. Icon của ô địa chỉ hero. */
@Composable
fun TerminalIcon(
    size: Dp = 20.dp,
    color: Color,
) {
    Canvas(modifier = Modifier.size(size)) {
        val stroke = STROKE_DP.dp.toPx()
        val w = this.size.width
        val h = this.size.height
        drawRoundRect(
            color = color,
            topLeft = Offset(stroke / 2, h * 0.1f + stroke / 2),
            size = Size(w - stroke, h * 0.8f - stroke),
            cornerRadius = CornerRadius(w * 0.16f),
            style = Stroke(width = stroke),
        )
        // Dấu ">" — hai đoạn gập, đầu tròn.
        val chevron =
            Path().apply {
                moveTo(w * 0.26f, h * 0.36f)
                lineTo(w * 0.42f, h * 0.5f)
                lineTo(w * 0.26f, h * 0.64f)
            }
        drawPath(
            path = chevron,
            color = color,
            style = Stroke(width = stroke, cap = StrokeCap.Round, join = StrokeJoin.Round),
        )
        // Dấu "_" của con trỏ chờ lệnh.
        drawLine(
            color = color,
            start = Offset(w * 0.52f, h * 0.64f),
            end = Offset(w * 0.72f, h * 0.64f),
            strokeWidth = stroke,
            cap = StrokeCap.Round,
        )
    }
}

/** lock — thân khoá bo góc + quai hình chữ U ngược. Icon của ô mật khẩu (GĐ10). */
@Composable
fun LockIcon(
    size: Dp = 18.dp,
    color: Color,
) {
    Canvas(modifier = Modifier.size(size)) {
        val stroke = STROKE_DP.dp.toPx()
        val w = this.size.width
        val h = this.size.height
        // Thân khoá: nửa dưới.
        drawRoundRect(
            color = color,
            topLeft = Offset(w * 0.2f, h * 0.46f),
            size = Size(w * 0.6f, h * 0.38f),
            cornerRadius = CornerRadius(w * 0.1f),
            style = Stroke(width = stroke),
        )
        // Quai: cung nửa trên, hai chân cắm xuống mép thân.
        val shackle =
            Path().apply {
                moveTo(w * 0.32f, h * 0.46f)
                lineTo(w * 0.32f, h * 0.32f)
                cubicTo(w * 0.32f, h * 0.14f, w * 0.68f, h * 0.14f, w * 0.68f, h * 0.32f)
                lineTo(w * 0.68f, h * 0.46f)
            }
        drawPath(
            path = shackle,
            color = color,
            style = Stroke(width = stroke, cap = StrokeCap.Round, join = StrokeJoin.Round),
        )
    }
}

/**
 * eye / eye.slash — nút hiện-ẩn mật khẩu. `crossed` gạch chéo qua để báo đang ẩn.
 */
@Composable
fun EyeIcon(
    size: Dp = 16.dp,
    color: Color,
    crossed: Boolean = false,
) {
    Canvas(modifier = Modifier.size(size)) {
        val stroke = STROKE_DP.dp.toPx()
        val w = this.size.width
        val h = this.size.height
        // Hình quả hạnh: hai cung đối xứng qua trục ngang.
        val eye =
            Path().apply {
                moveTo(w * 0.1f, h * 0.5f)
                cubicTo(w * 0.3f, h * 0.2f, w * 0.7f, h * 0.2f, w * 0.9f, h * 0.5f)
                cubicTo(w * 0.7f, h * 0.8f, w * 0.3f, h * 0.8f, w * 0.1f, h * 0.5f)
                close()
            }
        drawPath(path = eye, color = color, style = Stroke(width = stroke, join = StrokeJoin.Round))
        drawCircle(
            color = color,
            radius = w * 0.13f,
            center = Offset(w * 0.5f, h * 0.5f),
            style = Stroke(width = stroke),
        )
        if (crossed) {
            drawLine(
                color = color,
                start = Offset(w * 0.16f, h * 0.84f),
                end = Offset(w * 0.84f, h * 0.16f),
                strokeWidth = stroke,
                cap = StrokeCap.Round,
            )
        }
    }
}

/** macwindow — khung cửa sổ với thanh tiêu đề. Icon của dòng nguồn chia sẻ. */
@Composable
fun WindowIcon(
    size: Dp = 18.dp,
    color: Color,
) {
    Canvas(modifier = Modifier.size(size)) {
        val stroke = STROKE_DP.dp.toPx()
        val w = this.size.width
        val h = w * 0.78f
        val top = (this.size.height - h) / 2f
        drawRoundRect(
            color = color,
            topLeft = Offset(stroke / 2, top + stroke / 2),
            size = Size(w - stroke, h - stroke),
            cornerRadius = CornerRadius(w * 0.14f),
            style = Stroke(width = stroke),
        )
        // Thanh tiêu đề: một gạch ngang chia khung, đúng hình macwindow.
        val barY = top + h * 0.3f
        drawLine(
            color = color,
            start = Offset(stroke / 2, barY),
            end = Offset(w - stroke / 2, barY),
            strokeWidth = stroke,
        )
    }
}
