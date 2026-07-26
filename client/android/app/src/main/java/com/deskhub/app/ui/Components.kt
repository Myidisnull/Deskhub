// =============================================================================
// Components.kt — bộ thành phần của hệ thiết kế: chữ, bề mặt, nút, control, dòng
// dữ liệu. Đối ứng các file Design*.swift bên client/ios/app/swift (bản iOS tách
// sáu file vì Swift một-type-một-chuyện; Kotlin gộp một file cho đỡ nhảy qua lại —
// tổng cộng vẫn là đúng một tầng thành phần).
//
// KHÔNG CÓ TRẠNG THÁI "RÊ CHUỘT" — bậc NHẤN đi thẳng tới màu đích của desktop, và
// trạng thái "bấm được" do hình dáng lúc NGHỈ nói ra (viền tóc, nền kính). Xem ghi
// chú dài ở DesignButtons.swift bên iOS — mọi quyết định ở đây theo đúng bản đó.
//
// VÌ SAO KHÔNG DÙNG Button/Checkbox/OutlinedTextField CỦA MATERIAL
//   Control mặc định của Material mang nguyên bộ trang trí hệ thống (ripple tím,
//   viền focus theo colorScheme, chiều cao tối thiểu riêng) và không có cách nào bỏ
//   hết chúng mà không dựng lại control. Một ripple Material nổ giữa ô kính mint đọc
//   ra ngay là hai app khác nhau ghép lại.
//
// LIÊN QUAN: Tokens.kt (bảng token), Strings.kt (chữ), MainActivity/StreamActivity
// =============================================================================
package com.deskhub.app.ui

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.StrokeJoin
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp

// MARK: — Chữ

/** Eyebrow: mono, HOA, giãn chữ .2em. Nhãn nhỏ trên mỗi tiêu đề mục. */
@Composable
fun Eyebrow(
    text: String,
    dim: Boolean = false,
) {
    Text(
        text = text.uppercase(),
        fontFamily = Ds.mono,
        fontSize = Ds.textLabel,
        fontWeight = FontWeight.Medium,
        letterSpacing = Ds.trackLabel,
        color = if (dim) Ds.colors.textSecondary else Ds.colors.accent,
    )
}

/** Mono: MỌI con số sản phẩm hiện ra đều dùng kiểu này. */
@Composable
fun MonoText(
    text: String,
    size: TextUnit = Ds.textMonoSm,
    color: Color = Ds.colors.textSecondary,
    maxLines: Int = Int.MAX_VALUE,
) {
    Text(
        text = text,
        fontFamily = Ds.mono,
        fontSize = size,
        color = color,
        maxLines = maxLines,
        overflow = TextOverflow.Ellipsis,
    )
}

/**
 * Tiêu đề màn: eyebrow + tiêu đề phông hiển thị + chú thích mono xuống dòng riêng
 * (desktop đặt aside cùng hàng; màn hẹp thì nó ép tiêu đề xuống ba dòng).
 */
@Composable
fun ScreenHeader(
    eyebrow: String,
    title: String,
    aside: String? = null,
) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Eyebrow(text = eyebrow)
        Text(
            text = title,
            fontFamily = Ds.display,
            fontSize = Ds.textTitle,
            fontWeight = FontWeight.Bold,
            letterSpacing = Ds.trackDisplay,
            color = Ds.colors.textPrimary,
        )
        if (aside != null) MonoText(text = aside, maxLines = 1)
    }
}

/** Nhãn mục + gạch tóc kéo hết chiều ngang + đuôi tuỳ ý. */
@Composable
fun SectionHeader(
    label: String,
    trailing: @Composable () -> Unit = {},
) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Eyebrow(text = label, dim = true)
        Box(
            modifier =
                Modifier
                    .weight(1f)
                    .height(Ds.hairline)
                    .background(Ds.colors.borderHairline),
        )
        trailing()
    }
}

// MARK: — Bề mặt

/** Panel: hộp kính bo 18 có nhãn, dùng cho mọi khối nội dung phụ. */
@Composable
fun Panel(
    label: String,
    content: @Composable () -> Unit,
) {
    val shape = RoundedCornerShape(Ds.radiusXl)
    Column(
        modifier =
            Modifier
                .fillMaxWidth()
                .background(Ds.colors.surfaceCard, shape)
                .border(Ds.hairline, Ds.colors.borderHairline, shape)
                .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Eyebrow(text = label, dim = true)
        content()
    }
}

/** Chip: nhãn mono nhỏ, bo pill, CÓ viền tóc — siêu dữ liệu tĩnh. */
@Composable
fun Chip(
    text: String,
    active: Boolean = false,
) {
    val shape = RoundedCornerShape(Ds.radiusPill)
    Text(
        text = text,
        fontFamily = Ds.mono,
        fontSize = Ds.textMonoSm,
        color = if (active) Ds.colors.accent else Ds.colors.textSecondary,
        modifier =
            Modifier
                .background(if (active) Ds.colors.accentWash else Ds.colors.surfaceControl, shape)
                .border(Ds.hairline, if (active) Ds.colors.borderActive else Ds.colors.borderHairline, shape)
                .padding(horizontal = 10.dp, vertical = 4.dp),
    )
}

/**
 * HUD: thanh nổi trên video, bo pill, nền chrome mờ. Thứ DUY NHẤT đổ bóng — nhưng
 * bóng của Compose (Modifier.shadow) vẽ cả viền elevation nên ở đây chỉ dùng nền
 * đậm hơn: trên nền video thật thì độ tương phản của bgChrome đã đủ tách lớp.
 */
@Composable
fun HudBar(content: @Composable RowScope.() -> Unit) {
    val shape = RoundedCornerShape(Ds.radiusPill)
    Row(
        modifier =
            Modifier
                .background(Ds.colors.bgChrome, shape)
                .border(Ds.hairline, Ds.colors.borderHairline, shape)
                .padding(horizontal = 12.dp, vertical = 7.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        content = content,
    )
}

@Composable
fun HudDivider() {
    Box(
        modifier =
            Modifier
                .width(Ds.hairline)
                .height(16.dp)
                .background(Ds.colors.borderHairline),
    )
}

// MARK: — Trạng thái

/**
 * Chấm 8dp "đang sống / không". CHẤM SỐNG CÓ QUẦNG SÁNG, CHẤM CHẾT THÌ KHÔNG: hai
 * hình tròn cùng cỡ chỉ khác màu thì người mù màu xanh–đỏ không phân biệt được;
 * quầng thêm tín hiệu kích thước + độ chói mà ai cũng thấy.
 */
@Composable
fun StatusDot(live: Boolean) {
    Box(modifier = Modifier.size(16.dp), contentAlignment = Alignment.Center) {
        if (live) {
            Box(
                modifier =
                    Modifier
                        .size(16.dp)
                        .background(Ds.colors.accentWash, CircleShape),
            )
        }
        Box(
            modifier =
                Modifier
                    .size(8.dp)
                    .alpha(if (live) 1f else 0.55f)
                    .background(if (live) Ds.colors.accent else Ds.colors.statusIdle, CircleShape),
        )
    }
}

enum class PillTone { LIVE, NEUTRAL, DANGER }

/**
 * Nhãn trạng thái mono viết HOA. BA TÔNG, KHÔNG PHẢI BA MÀU TUỲ Ý: sống = mint,
 * trung tính = xám trên kính, đứt = đỏ. Pill KHÔNG VIỀN, khác Chip: chip là siêu dữ
 * liệu tĩnh, pill là trạng thái đang chạy.
 */
@Composable
fun StatePill(
    text: String,
    tone: PillTone = PillTone.NEUTRAL,
) {
    val fg =
        when (tone) {
            PillTone.LIVE -> Ds.colors.accent
            PillTone.NEUTRAL -> Ds.colors.textSecondary
            PillTone.DANGER -> Ds.colors.statusError
        }
    val bg =
        when (tone) {
            PillTone.LIVE -> Ds.colors.accentWash
            PillTone.NEUTRAL -> Ds.colors.surfaceControl
            PillTone.DANGER -> Ds.colors.dangerWash
        }
    Text(
        text = text.uppercase(),
        fontFamily = Ds.mono,
        fontSize = Ds.textLabel,
        // .1em chứ không phải .2em của eyebrow: pill là một từ trong hộp hẹp.
        letterSpacing = 0.1.em,
        color = fg,
        modifier =
            Modifier
                .background(bg, RoundedCornerShape(Ds.radiusPill))
                .padding(horizontal = 10.dp, vertical = 4.dp),
    )
}

/** Nhãn nhỏ trên, con số to dưới — số để LIẾC nên dùng phông hiển thị, không mono. */
@Composable
fun StatBlock(
    label: String,
    value: String,
) {
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Eyebrow(text = label, dim = true)
        Text(
            text = value,
            fontFamily = Ds.display,
            fontSize = Ds.textStat,
            fontWeight = FontWeight.Bold,
            letterSpacing = Ds.trackTitle,
            color = Ds.colors.textPrimary,
        )
    }
}

/**
 * Biểu đồ đường độ trễ — chép 1-1 từ Sparkline bên client/ios/app/swift/
 * DesignSurfaces.swift: một đường mint mảnh, không trục, không lưới, không nhãn.
 *
 * THANG ĐỨNG TỰ ĐỘNG THEO DỮ LIỆU ĐANG HIỆN — không neo đáy vào 0. Độ trễ dao động
 * 4–7 ms trên thang 0–7 gần như là một đường thẳng, mà chính cái dao động đó là thứ
 * người dùng nhìn biểu đồ để xem.
 *
 * @param values dãy mẫu theo thứ tự CŨ → MỚI, vẽ trái sang phải.
 */
@Composable
fun Sparkline(
    values: List<Double>,
    modifier: Modifier,
) {
    val accent = Ds.colors.accent
    Canvas(modifier = modifier) {
        if (values.size < 2 || size.width <= 1f || size.height <= 1f) return@Canvas
        val lo = values.min()
        val hi = values.max()
        val span = hi - lo
        val strokeWidth = 1.5.dp.toPx()

        // Dãy phẳng hoàn toàn: span = 0 sẽ chia cho 0. Vẽ nó thành một đường ngang
        // giữa khung — đó đúng là sự thật của dữ liệu.
        if (span <= 1e-6) {
            drawLine(
                color = accent,
                start = Offset(0f, size.height / 2f),
                end = Offset(size.width, size.height / 2f),
                strokeWidth = strokeWidth,
                cap = StrokeCap.Round,
            )
            return@Canvas
        }

        val pad = 0.12f // chừa mép trên/dưới để đường không dán vào viền
        val usable = size.height * (1f - 2f * pad)
        val step = size.width / (values.size - 1)
        val path = Path()
        values.forEachIndexed { idx, sample ->
            val norm = ((sample - lo) / span).toFloat()
            // Trục y của màn hình hướng XUỐNG, nên mẫu lớn nhất phải nằm ở trên.
            val posY = size.height * pad + (1f - norm) * usable
            val posX = idx * step
            if (idx == 0) path.moveTo(posX, posY) else path.lineTo(posX, posY)
        }
        drawPath(
            path = path,
            color = accent,
            style = Stroke(width = strokeWidth, cap = StrokeCap.Round, join = StrokeJoin.Round),
        )
    }
}

/** Vòng quay — cùng biểu đồ độ trễ là hai chuyển động liên tục DUY NHẤT của sản phẩm. */
@Composable
fun Spinner(size: Dp = 16.dp) {
    val transition = rememberInfiniteTransition(label = "spinner")
    val angle by transition.animateFloat(
        initialValue = 0f,
        targetValue = 360f,
        animationSpec =
            infiniteRepeatable(
                animation = tween(durationMillis = 900, easing = LinearEasing),
            ),
        label = "angle",
    )
    val accent = Ds.colors.accent
    Canvas(modifier = Modifier.size(size)) {
        drawArc(
            color = accent,
            startAngle = angle,
            sweepAngle = 259f, // 0.72 vòng, trùng bản Swift
            useCenter = false,
            style = Stroke(width = 1.8.dp.toPx(), cap = StrokeCap.Round),
        )
    }
}

// MARK: — Nút

enum class DsButtonVariant { PRIMARY, SECONDARY, GHOST, DANGER }

enum class DsButtonSize { SM, MD, LG }

/**
 * Nút của hệ thiết kế. PRIMARY: nền mint đặc, mỗi màn CHỈ MỘT — nó là câu trả lời
 * cho "tôi tới màn này để làm gì?". SECONDARY: kính + viền tóc. GHOST: không nền
 * không viền. DANGER: chỉ dùng cho việc CẮT ĐỨT (Ngắt, Kết thúc).
 */
@Composable
fun DsButton(
    text: String,
    onClick: () -> Unit,
    variant: DsButtonVariant = DsButtonVariant.SECONDARY,
    size: DsButtonSize = DsButtonSize.MD,
    enabled: Boolean = true,
    pill: Boolean = false,
    fullWidth: Boolean = false,
) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    val colors = Ds.colors

    val height =
        when (size) {
            DsButtonSize.SM -> Ds.controlHeightSm
            DsButtonSize.MD -> Ds.controlHeight
            DsButtonSize.LG -> Ds.controlHeightLg
        }
    val hPad =
        when (size) {
            DsButtonSize.SM -> 12.dp
            DsButtonSize.MD -> 18.dp
            DsButtonSize.LG -> 24.dp
        }
    val fontSize =
        when (size) {
            DsButtonSize.SM -> Ds.textBodySm
            DsButtonSize.MD -> Ds.textBody
            DsButtonSize.LG -> Ds.textBodyLg
        }
    val shape =
        RoundedCornerShape(
            if (pill) {
                Ds.radiusPill
            } else if (size == DsButtonSize.SM) {
                Ds.radiusSm
            } else {
                Ds.radiusMd
            },
        )

    val bg by animateColorAsState(
        targetValue =
            when (variant) {
                DsButtonVariant.PRIMARY ->
                    if (!enabled) {
                        colors.accentDisabled
                    } else if (pressed) {
                        colors.accentPress
                    } else {
                        colors.accent
                    }
                DsButtonVariant.SECONDARY -> if (pressed) colors.surfaceControlPress else colors.surfaceControl
                DsButtonVariant.GHOST -> if (pressed) colors.surfaceControl else Color.Transparent
                DsButtonVariant.DANGER -> colors.dangerWash
            },
        animationSpec = tween(Ds.EASE_MS),
        label = "buttonBg",
    )
    val border =
        when (variant) {
            DsButtonVariant.PRIMARY ->
                if (!enabled) {
                    Color.Transparent
                } else if (pressed) {
                    colors.accentPress
                } else {
                    colors.accent
                }
            DsButtonVariant.SECONDARY -> colors.borderHairline
            DsButtonVariant.GHOST -> Color.Transparent
            DsButtonVariant.DANGER -> colors.dangerLine
        }
    val fg =
        when (variant) {
            DsButtonVariant.PRIMARY -> if (enabled) colors.textOnAccent else colors.textSecondary
            DsButtonVariant.SECONDARY -> colors.textPrimary
            DsButtonVariant.GHOST -> if (pressed) colors.accent else colors.textSecondary
            DsButtonVariant.DANGER -> colors.statusError
        }
    // Nút chính bị TẮT thì KHÔNG mờ đi mà đổi hẳn sang nền xám (một nút mint mờ vẫn
    // đọc là "mint", tức vẫn mời bấm); các loại khác mờ 45%. Danger nhạt nhẹ lúc nhấn
    // — nút cắt đứt phiên không nên có thêm gì mời gọi.
    val alpha =
        if (!enabled) {
            if (variant == DsButtonVariant.PRIMARY) 1f else 0.45f
        } else if (variant == DsButtonVariant.DANGER && pressed) {
            0.8f
        } else {
            1f
        }

    Box(
        modifier =
            Modifier
                .let { if (fullWidth) it.fillMaxWidth() else it }
                .height(height)
                .alpha(alpha)
                .background(bg, shape)
                .border(Ds.hairline, border, shape)
                // indication = null: ripple của Material là trang trí của HỆ, không
                // phải của hệ thiết kế này — phản hồi nhấn là ĐỔI MÀU NỀN ở trên.
                .clickable(
                    interactionSource = interaction,
                    indication = null,
                    enabled = enabled,
                    onClick = onClick,
                ).padding(horizontal = hPad),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = text,
            fontSize = fontSize,
            fontWeight = FontWeight.SemiBold,
            color = fg,
            maxLines = 1,
        )
    }
}

/**
 * Nút chỉ có biểu tượng (nội dung tuỳ ý) — thanh trên cùng và HUD màn xem.
 * LÚC NGHỈ VẪN CÓ Ô KÍNH + VIỀN: trên cảm ứng không có con trỏ đi dò đường, một biểu
 * tượng trần không nói được nó bấm được hay chỉ là hình trang trí.
 */
@Composable
fun DsIconButton(
    onClick: () -> Unit,
    side: Dp = Ds.iconButton,
    radius: Dp = Ds.radiusMd,
    active: Boolean = false,
    enabled: Boolean = true,
    content: @Composable () -> Unit,
) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    val shape = RoundedCornerShape(radius)
    val bg =
        if (active) {
            Ds.colors.accentWash
        } else if (pressed) {
            Ds.colors.surfaceControlPress
        } else {
            Ds.colors.surfaceControl
        }
    Box(
        modifier =
            Modifier
                .size(side)
                .alpha(if (enabled) 1f else 0.45f)
                .background(bg, shape)
                .border(Ds.hairline, if (active) Ds.colors.borderActive else Ds.colors.borderHairline, shape)
                .clickable(
                    interactionSource = interaction,
                    indication = null,
                    enabled = enabled,
                    onClick = onClick,
                ),
        contentAlignment = Alignment.Center,
    ) {
        content()
    }
}

// MARK: — Control

/**
 * Ô tick 22dp bo 7, tô mint khi tick, dấu tick vẽ bằng Path. VÙNG BẤM PHỦ CẢ NHÃN
 * và cao 44dp — bắt ngón tay nhắm vào một ô 22dp trong khi có sẵn dòng chữ ngay cạnh
 * là làm khó không đổi lấy gì.
 */
@Composable
fun DsCheckbox(
    checked: Boolean,
    onToggle: (Boolean) -> Unit,
    label: String,
) {
    val colors = Ds.colors
    Row(
        modifier =
            Modifier
                .defaultMinSize(minHeight = Ds.controlHeight)
                .clickable(
                    interactionSource = remember { MutableInteractionSource() },
                    indication = null,
                ) { onToggle(!checked) },
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        val fill by animateColorAsState(
            targetValue = if (checked) colors.accent else colors.checkboxOff,
            animationSpec = tween(Ds.EASE_MS),
            label = "checkFill",
        )
        Canvas(modifier = Modifier.size(22.dp)) {
            val corner = CornerRadius(7.dp.toPx())
            drawRoundRect(color = fill, cornerRadius = corner)
            drawRoundRect(
                color = if (checked) colors.accent else colors.borderHairline,
                cornerRadius = corner,
                style = Stroke(width = 1.dp.toPx()),
            )
            if (checked) {
                // Dấu tick: cùng ba điểm với bản Swift, dời vào giữa ô 22dp.
                val path =
                    Path().apply {
                        moveTo(5.2.dp.toPx(), 11.4.dp.toPx())
                        lineTo(9.4.dp.toPx(), 15.6.dp.toPx())
                        lineTo(16.8.dp.toPx(), 7.0.dp.toPx())
                    }
                drawPath(
                    path = path,
                    color = colors.textOnAccent,
                    style = Stroke(width = 2.4.dp.toPx(), cap = StrokeCap.Round, join = StrokeJoin.Round),
                )
            }
        }
        Text(
            text = label,
            fontSize = Ds.textBodySm,
            color = colors.textPrimary,
        )
    }
}

/**
 * Ô địa chỉ hero: MỘT hàng có viền chứa [biểu tượng][ô nhập][đuôi], cao 58, bo 16.
 * Viền đổi sang mint khi có tiêu điểm. Đuôi để dành cho vòng quay lúc đang hỏi host
 * — nút Kết nối thật nằm ở thanh đáy, trong tầm ngón cái.
 */
@Composable
fun HeroField(
    value: String,
    onValueChange: (String) -> Unit,
    placeholder: String,
    onGo: () -> Unit,
    trailing: @Composable () -> Unit = {},
) {
    val interaction = remember { MutableInteractionSource() }
    val focused by interaction.collectIsFocusedAsState()
    val shape = RoundedCornerShape(Ds.radiusLg)
    val borderColor by animateColorAsState(
        targetValue = if (focused) Ds.colors.borderActive else Ds.colors.borderHairline,
        animationSpec = tween(Ds.EASE_MS),
        label = "fieldBorder",
    )

    Row(
        modifier =
            Modifier
                .fillMaxWidth()
                .height(Ds.fieldHeight)
                .background(Ds.colors.surfaceField, shape)
                .border(Ds.hairline, borderColor, shape)
                .padding(horizontal = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        // Icon terminal vẽ tay (Icons.kt) — cùng hình với SF Symbol "terminal" bên
        // iOS: khung bo góc + dấu nhắc >_. 20dp trùng cỡ symbol 18pt của bản iOS.
        TerminalIcon(size = 20.dp, color = Ds.colors.accent)
        BasicTextField(
            value = value,
            onValueChange = onValueChange,
            modifier = Modifier.weight(1f),
            interactionSource = interaction,
            textStyle =
                TextStyle(
                    fontFamily = Ds.mono,
                    fontSize = 17.sp,
                    color = Ds.colors.textPrimary,
                ),
            cursorBrush = SolidColor(Ds.colors.accent),
            singleLine = true,
            keyboardOptions =
                KeyboardOptions(
                    // Text chứ không phải Number/Decimal: bàn phím số theo locale có
                    // máy thiếu dấu chấm hoặc dấu hai chấm — không gõ nổi "ip:port".
                    keyboardType = KeyboardType.Ascii,
                    imeAction = ImeAction.Go,
                ),
            keyboardActions = KeyboardActions(onGo = { onGo() }),
            decorationBox = { inner ->
                Box(contentAlignment = Alignment.CenterStart) {
                    if (value.isEmpty()) {
                        Text(
                            text = placeholder,
                            fontFamily = Ds.mono,
                            fontSize = 17.sp,
                            color = Ds.colors.textSecondary,
                        )
                    }
                    inner()
                }
            },
        )
        trailing()
    }
}

// MARK: — Dòng dữ liệu

/**
 * Thẻ máy đã nhớ: chấm trạng thái + tên, địa chỉ mono, chip loại đường. CẢ THẺ LÀ MỘT
 * NÚT, và bấm chỉ ĐIỀN SẴN địa chỉ chứ không nối thẳng: nối thẳng sẽ bỏ qua ô "chỉ
 * xem", mà đó là lựa chọn cần cân nhắc TRƯỚC khi gõ vào máy người khác.
 */
@Composable
fun MachineCard(
    name: String,
    address: String,
    link: String,
    onTap: () -> Unit,
) {
    CardRow(onTap = onTap, selected = false) {
        StatusDot(live = false)
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(3.dp),
        ) {
            Text(
                text = name,
                fontSize = Ds.textBodyLg,
                fontWeight = FontWeight.SemiBold,
                color = Ds.colors.textPrimary,
                maxLines = 1,
                overflow = TextOverflow.MiddleEllipsis,
            )
            // Host chưa xưng tên thì name CHÍNH LÀ địa chỉ — đừng in nó hai lần.
            if (name != address) MonoText(text = address, maxLines = 1)
        }
        if (link.isNotEmpty()) Chip(text = link)
    }
}

/**
 * Dòng nguồn trên màn chọn: [tick] tên + kích thước + pill trạng thái. BẤM ĐÂU TRONG
 * DÒNG CŨNG CHỌN, và ô tick hành xử như radio — dh_start nhận MỘT sourceId.
 */
@Composable
fun SourceRow(
    name: String,
    detail: String,
    selected: Boolean,
    state: String,
    tone: PillTone,
    onSelect: () -> Unit,
) {
    CardRow(onTap = onSelect, selected = selected) {
        // Ô tick chỉ để NHÌN (decorative) — cả dòng mới là cái nút.
        DecorativeCheck(checked = selected)
        // Icon cửa sổ — trùng "macwindow" bên iOS. Giao thức không nói nguồn là cửa
        // sổ hay màn hình ở phía client (DHSourceInfo chỉ có tên + kích thước), nên
        // đừng đoán: một biểu tượng cửa sổ dùng chung.
        WindowIcon(
            size = 18.dp,
            color = if (selected) Ds.colors.accent else Ds.colors.textSecondary,
        )
        Column(
            modifier = Modifier.weight(1f),
            verticalArrangement = Arrangement.spacedBy(3.dp),
        ) {
            Text(
                text = name,
                fontSize = Ds.textBodyLg,
                fontWeight = FontWeight.SemiBold,
                color = Ds.colors.textPrimary,
                maxLines = 1,
                overflow = TextOverflow.MiddleEllipsis,
            )
            MonoText(text = detail, maxLines = 1)
        }
        StatePill(text = state, tone = tone)
    }
}

/** Khung chung của hai dòng trên: kính bo 16, viền mint khi được chọn/nhấn. */
@Composable
private fun CardRow(
    onTap: () -> Unit,
    selected: Boolean,
    content: @Composable RowScope.() -> Unit,
) {
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    val shape = RoundedCornerShape(Ds.radiusLg)
    val bg by animateColorAsState(
        targetValue =
            if (pressed) {
                Ds.colors.surfaceCardPress
            } else if (selected) {
                Ds.colors.accentWash
            } else {
                Ds.colors.surfaceCard
            },
        animationSpec = tween(Ds.EASE_MS),
        label = "cardBg",
    )
    Row(
        modifier =
            Modifier
                .fillMaxWidth()
                .background(bg, shape)
                .border(
                    Ds.hairline,
                    if (selected || pressed) Ds.colors.borderActive else Ds.colors.borderHairline,
                    shape,
                ).clickable(
                    interactionSource = interaction,
                    indication = null,
                    onClick = onTap,
                ).padding(14.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        content = content,
    )
}

/** Ô tick chỉ-để-nhìn trong dòng nguồn — không tự nhận bấm. */
@Composable
private fun DecorativeCheck(checked: Boolean) {
    val colors = Ds.colors
    val fill by animateColorAsState(
        targetValue = if (checked) colors.accent else colors.checkboxOff,
        animationSpec = tween(Ds.EASE_MS),
        label = "decorCheckFill",
    )
    Canvas(modifier = Modifier.size(22.dp)) {
        val corner = CornerRadius(7.dp.toPx())
        drawRoundRect(color = fill, cornerRadius = corner)
        drawRoundRect(
            color = if (checked) colors.accent else colors.borderHairline,
            cornerRadius = corner,
            style = Stroke(width = 1.dp.toPx()),
        )
        if (checked) {
            val path =
                Path().apply {
                    moveTo(5.2.dp.toPx(), 11.4.dp.toPx())
                    lineTo(9.4.dp.toPx(), 15.6.dp.toPx())
                    lineTo(16.8.dp.toPx(), 7.0.dp.toPx())
                }
            drawPath(
                path = path,
                color = colors.textOnAccent,
                style = Stroke(width = 2.4.dp.toPx(), cap = StrokeCap.Round, join = StrokeJoin.Round),
            )
        }
    }
}

// MARK: — Thanh trên cùng

/**
 * Danh tính app bên trái, sáng/tối + ngôn ngữ bên phải — đúng bộ đôi mà bản desktop
 * đặt ở chân rail (rail dọc không sang mobile: nó ăn 19% bề ngang màn hình để phục
 * vụ một menu chỉ có MỘT đích thật — mobile là client-only).
 * `leading` cho màn con đặt nút quay lại vào chỗ của danh tính.
 */
@Composable
fun TopBar(leading: @Composable RowScope.() -> Unit = { AppMark() }) {
    Row(
        modifier =
            Modifier
                .fillMaxWidth()
                .padding(horizontal = Ds.screenGutter, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        leading()
        Box(modifier = Modifier.weight(1f))

        DsIconButton(onClick = { AppState.toggleTheme() }, side = 38.dp, radius = Ds.radiusSm) {
            // Icon vẽ tay (Icons.kt) chứ không phải ký tự văn bản: glyph văn bản do
            // font hệ thống quyết định cỡ và baseline nên bé và lệch so với SF
            // Symbols bên iOS — 20dp ở đây trùng cỡ sun.max/moon 17pt trong ô 38.
            if (AppState.isDark) {
                SunIcon(size = 20.dp, color = Ds.colors.textPrimary)
            } else {
                MoonIcon(size = 20.dp, color = Ds.colors.textPrimary)
            }
        }
        DsIconButton(onClick = { AppState.toggleLang() }, side = 38.dp, radius = Ds.radiusSm, active = true) {
            Text(
                text = AppState.lang.uppercase(),
                fontFamily = Ds.mono,
                fontSize = Ds.textBodySm,
                fontWeight = FontWeight.Medium,
                color = Ds.colors.accent,
            )
        }
    }
}

/** Danh tính: chấm mint + tên sản phẩm. */
@Composable
fun AppMark() {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Box(
            modifier =
                Modifier
                    .size(8.dp)
                    .background(Ds.colors.accent, CircleShape),
        )
        Text(
            text = "Deskhub",
            fontFamily = Ds.display,
            fontSize = Ds.textBodyLg,
            fontWeight = FontWeight.SemiBold,
            letterSpacing = Ds.trackTitle,
            color = Ds.colors.textPrimary,
        )
    }
}

// MARK: — Nguồn sáng

/**
 * Quầng cobalt sau bố cục — KHÔNG BAO GIỜ là một lớp nền. Vẽ bằng drawBehind trên cả
 * màn: một vệt sáng hắt từ góc trên phải vào.
 *
 * TÂM QUẦNG TRÙNG CÔNG THỨC BẢN iOS (Glow trong DesignText.swift): side = bề ngang
 * × 1.35, tâm tại (W − 0.22·side, 0.16·side) — là kết quả rút gọn của "căn
 * topTrailing rồi offset (0.28·side, −0.34·side)" bên SwiftUI. Hai màn cùng cỡ phải
 * ra cùng một vệt sáng.
 */
@Composable
fun Modifier.cobaltGlow(): Modifier {
    val glow = Ds.colors.glowCobalt
    return drawBehind {
        val side = maxOf(size.width, 320.dp.toPx()) * 1.35f
        val center = Offset(size.width - side * 0.22f, side * 0.16f)
        drawCircle(
            brush =
                Brush.radialGradient(
                    0f to glow,
                    0.62f to glow.copy(alpha = 0f),
                    center = center,
                    radius = side / 2f,
                ),
            radius = side / 2f,
            center = center,
        )
    }
}
