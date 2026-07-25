// =============================================================================
// DesignButtons.swift — bốn biến thể nút, nút biểu tượng, thẻ bấm được, và Tile.
//
// TRẠNG THÁI LÀ ĐỔI MÀU, KHÔNG PHẢI ĐỔI ĐỘ MỜ
//   Bản thiết kế ghi rõ từng biến thể đổi gì khi rê/nhấn: primary chạy
//   #66e3b8 → #48d3a3 (rê) → #2bb98a (nhấn); mặt kính nâng 4.5% → 7%; ghost chỉ đổi
//   màu CHỮ; thẻ chọn được thì đổi viền sang mint. Làm mờ cả nút đi là hiệu ứng KHÁC
//   HẲN — nó kéo cả chữ lẫn viền nhạt theo, nên ô kính rê chuột vào trông TỐI đi
//   trong khi bản thiết kế cho nó SÁNG lên. Chỉ trạng thái TẮT mới dùng độ mờ.
//
// VÌ SAO KHÔNG DÙNG .borderedProminent CỦA HỆ
//   Nút mặc định của AppKit lấy màu nhấn của HỆ ĐIỀU HÀNH (xanh dương, mà người dùng
//   đổi được sang tím, hồng…). Cả hệ thiết kế này xoay quanh đúng MỘT màu tín hiệu là
//   mint; để lọt màu nhấn hệ thống vào là mất luôn ý nghĩa của nó.
//
// LIÊN QUAN: DesignTokens.swift (bảng token), DesignSurfaces.swift
// =============================================================================
import AppKit
import SwiftUI

// MARK: - Nút

enum DSButtonSize {
    case sm, md, lg

    var height: CGFloat {
        switch self {
        case .sm: DS.controlHeightSm
        case .md: DS.controlHeight
        case .lg: DS.controlHeightLg
        }
    }

    var hPadding: CGFloat {
        switch self {
        case .sm: 12
        case .md: 16
        case .lg: 22
        }
    }

    var font: Font {
        switch self {
        case .sm: DS.ui(DS.textBodySm, weight: .semibold)
        case .md: DS.ui(DS.textBody, weight: .semibold)
        case .lg: DS.ui(DS.textBodyLg, weight: .semibold)
        }
    }

    var radius: CGFloat {
        switch self {
        case .sm: DS.radiusSm
        case .md, .lg: DS.radiusMd
        }
    }
}

enum DSButtonVariant {
    /// Nền mint đặc. Mỗi màn CHỈ MỘT nút loại này — nó là câu trả lời cho
    /// "tôi tới màn này để làm gì?".
    case primary
    /// Kính + viền tóc. Hành động bình thường.
    case secondary
    /// Không nền, không viền. Hành động phụ cạnh một hành động thật.
    case ghost
    /// Ghost đổi sang MINT khi rê — nút chép cạnh một địa chỉ. Nó không phải hành động
    /// phụ mà là đường tắt DUY NHẤT thay cho việc chép tay một chuỗi số.
    case ghostAccent
    /// Chỉ dùng cho việc CẮT ĐỨT (Ngắt, Dừng hết, Kết thúc).
    case danger
}

struct DSButtonStyle: ButtonStyle {
    var variant: DSButtonVariant = .secondary
    var size: DSButtonSize = .md
    var pill = false
    var fullWidth = false

    func makeBody(configuration: Configuration) -> some View {
        StyledLabel(configuration: configuration, variant: variant, size: size, pill: pill, fullWidth: fullWidth)
    }

    private struct StyledLabel: View {
        let configuration: Configuration
        let variant: DSButtonVariant
        let size: DSButtonSize
        let pill: Bool
        let fullWidth: Bool

        @Environment(\.isEnabled) private var enabled
        @State private var hovering = false

        private var pressed: Bool { configuration.isPressed }
        private var radius: CGFloat { pill ? DS.radiusPill : size.radius }

        var body: some View {
            configuration.label
                .font(size.font)
                .foregroundStyle(foreground)
                .padding(.horizontal, size.hPadding)
                .frame(height: size.height)
                .frame(maxWidth: fullWidth ? .infinity : nil)
                .background(background, in: RoundedRectangle(cornerRadius: radius, style: .continuous))
                .overlay(
                    RoundedRectangle(cornerRadius: radius, style: .continuous)
                        .strokeBorder(border, lineWidth: DS.hairline)
                )
                // Quầng mint chỉ có ở nút chính, và chỉ khi nó ĐANG mời bấm: nút chính
                // bị tắt thì không có quầng — bản thiết kế dùng trạng thái tắt như một
                // lời chỉ dẫn ("chưa gõ địa chỉ thì chưa bấm được"), mà quầng sáng thì
                // lại đang mời bấm.
                .shadow(color: glowColor, radius: 20, y: 12)
                .opacity(dimmed)
                .contentShape(RoundedRectangle(cornerRadius: radius, style: .continuous))
                .onHover { hovering = $0 }
                .animation(DS.easeStandard, value: hovering)
                .animation(DS.easeStandard, value: pressed)
        }

        private var glowColor: Color {
            guard variant == .primary, enabled, hovering || pressed else { return .clear }
            return DS.glowAccent
        }

        // Nút chính bị TẮT thì KHÔNG mờ đi mà đổi hẳn sang nền xám: một nút mint mờ
        // vẫn đọc là "mint", tức là vẫn mời bấm.
        private var dimmed: Double {
            if !enabled { return variant == .primary ? 1 : 0.45 }
            // Danger không đổi gì khi rê — nút cắt đứt phiên không nên có thêm bất cứ
            // thứ gì mời gọi bấm vào. Nó chỉ nhạt đi một chút lúc đang bị nhấn.
            if variant == .danger, pressed { return 0.8 }
            return 1
        }

        private var background: Color {
            switch variant {
            case .primary:
                if !enabled { return DS.accentDisabled }
                if pressed { return DS.accentPress }
                return hovering ? DS.accentHover : DS.accent
            case .secondary:
                if pressed { return DS.surfaceControlPress }
                return hovering ? DS.surfaceControlHover : DS.surfaceControl
            case .ghost, .ghostAccent:
                return pressed && variant == .ghost ? DS.surfaceControl : .clear
            case .danger:
                return DS.dangerWash
            }
        }

        private var border: Color {
            switch variant {
            case .primary:
                if !enabled { return .clear }
                if pressed { return DS.accentPress }
                return hovering ? DS.accentHover : DS.accent
            case .secondary: return DS.borderHairline
            case .ghost, .ghostAccent: return Color.clear
            case .danger: return DS.dangerLine
            }
        }

        private var foreground: Color {
            switch variant {
            case .primary: enabled ? DS.textOnAccent : DS.textSecondary
            case .secondary: DS.textPrimary
            case .ghost: hovering || pressed ? DS.textPrimary : DS.textSecondary
            case .ghostAccent:
                if pressed {
                    DS.accentPress
                } else if hovering {
                    DS.accent
                } else {
                    DS.textSecondary
                }
            case .danger: DS.statusError
            }
        }
    }
}

// Nút chỉ có biểu tượng: trong suốt khi nghỉ, rê vào thì HIỆN RA một ô kính có viền.
// Dùng ở rail và HUD.
struct DSIconButtonStyle: ButtonStyle {
    var side: CGFloat = DS.railItem
    var radius: CGFloat = DS.radiusMd
    /// Mục đang được chọn: rửa mint + viền mint + biểu tượng mint, và KHÔNG đổi gì khi
    /// rê chuột — mục đang chọn mà chuyển sang màu kính-xám thì trông như vừa BỎ chọn.
    var active = false

    func makeBody(configuration: Configuration) -> some View {
        StyledLabel(configuration: configuration, side: side, radius: radius, active: active)
    }

    private struct StyledLabel: View {
        let configuration: Configuration
        let side: CGFloat
        let radius: CGFloat
        let active: Bool

        @Environment(\.isEnabled) private var enabled
        @State private var hovering = false

        var body: some View {
            configuration.label
                .foregroundStyle(foreground)
                .frame(width: side, height: side)
                .background(background, in: RoundedRectangle(cornerRadius: radius, style: .continuous))
                .overlay(
                    RoundedRectangle(cornerRadius: radius, style: .continuous)
                        .strokeBorder(border, lineWidth: DS.hairline)
                )
                .opacity(enabled ? 1 : 0.45)
                .contentShape(RoundedRectangle(cornerRadius: radius, style: .continuous))
                .onHover { hovering = $0 }
                .animation(DS.easeStandard, value: hovering)
        }

        private var background: Color {
            if active { return DS.accentWash }
            if configuration.isPressed { return DS.surfaceControlPress }
            return hovering ? DS.surfaceControl : .clear
        }

        private var border: Color {
            if active { return DS.borderActive }
            return hovering || configuration.isPressed ? DS.borderHairline : .clear
        }

        private var foreground: Color {
            if active { return DS.accent }
            return hovering || configuration.isPressed ? DS.textPrimary : DS.textSecondary
        }
    }
}

// Thẻ bấm được (thẻ máy, dòng nguồn chọn được): nền kính nâng một nấc VÀ viền đổi sang
// mint. Viền mint là thứ nói "bấm được vào đây", nên nó chỉ có ở những thứ thật sự
// bấm được — nút thường chỉ nâng nền kính lên một nấc.
struct DSCardButtonStyle: ButtonStyle {
    var radius: CGFloat = DS.radiusLg
    var padding: EdgeInsets = .init(top: 14, leading: 16, bottom: 14, trailing: 16)

    func makeBody(configuration: Configuration) -> some View {
        StyledLabel(configuration: configuration, radius: radius, padding: padding)
    }

    private struct StyledLabel: View {
        let configuration: Configuration
        let radius: CGFloat
        let padding: EdgeInsets

        @Environment(\.isEnabled) private var enabled
        @State private var hovering = false

        var body: some View {
            configuration.label
                .padding(padding)
                .frame(maxWidth: .infinity, alignment: .leading)
                .background(background, in: RoundedRectangle(cornerRadius: radius, style: .continuous))
                .overlay(
                    RoundedRectangle(cornerRadius: radius, style: .continuous)
                        .strokeBorder(border, lineWidth: DS.hairline)
                )
                .opacity(enabled ? 1 : 0.45)
                .contentShape(RoundedRectangle(cornerRadius: radius, style: .continuous))
                .onHover { hovering = $0 }
                .animation(DS.easeStandard, value: hovering)
        }

        private var background: Color {
            if configuration.isPressed { return DS.surfaceControlPress }
            return hovering ? DS.surfaceCardHover : DS.surfaceCard
        }

        private var border: Color {
            hovering || configuration.isPressed ? DS.borderActive : DS.borderHairline
        }
    }
}

// MARK: - Tile

// Ô lớn ở màn chính — hai việc duy nhất app này làm. CẢ Ô là cái nút, và khi rê chuột
// nó SÁNG LÊN MÀU MINT: nền mint 14%, viền mint, biểu tượng và dòng hành động chuyển
// sang mint. Dòng cuối là một dòng chữ mono viết HOA đóng vai "nút", không phải một
// Button thật — có hai vùng bấm lồng nhau là thứ người dùng không đoán được.
struct Tile: View {
    let icon: String
    let title: String
    let detail: String
    /// Dòng cuối ("TAKE CONTROL →"). Nó TRÔNG như một nút nhưng là chữ: có hai vùng
    /// bấm lồng nhau trong một ô mà cả ô đã bấm được là thứ người dùng không đoán ra.
    let action: String
    let onTap: () -> Void

    @State private var hovering = false

    var body: some View {
        Button(action: onTap) {
            VStack(alignment: .leading, spacing: 14) {
                Image(systemName: icon)
                    .font(.system(size: 26, weight: .regular))
                    .foregroundStyle(hovering ? DS.accent : DS.textPrimary)
                Text(title)
                    .font(DS.display(DS.textSubtitle))
                    .tracking(DS.trackTitle(DS.textSubtitle))
                    .foregroundStyle(DS.textPrimary)
                    .fixedSize(horizontal: false, vertical: true)
                Text(detail)
                    .font(DS.ui(DS.textBodySm))
                    .foregroundStyle(DS.textSecondary)
                    .lineSpacing(4)
                    .fixedSize(horizontal: false, vertical: true)
                Text(action.uppercased())
                    .font(DS.mono(DS.textLabel, weight: .medium))
                    .tracking(DS.trackLabel(DS.textLabel))
                    .foregroundStyle(hovering ? DS.accent : DS.textSecondary)
                    .padding(.top, 4)
            }
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(26)
            .background(
                hovering ? DS.accentWash : DS.surfaceCard,
                in: RoundedRectangle(cornerRadius: DS.radiusXl, style: .continuous)
            )
            .overlay(
                RoundedRectangle(cornerRadius: DS.radiusXl, style: .continuous)
                    .strokeBorder(hovering ? DS.borderActive : DS.borderHairline, lineWidth: DS.hairline)
            )
            .shadow(color: hovering ? DS.glowAccent : .clear, radius: 20, y: 12)
            .contentShape(RoundedRectangle(cornerRadius: DS.radiusXl, style: .continuous))
        }
        .buttonStyle(.plain)
        .onHover { hovering = $0 }
        .animation(DS.easeStandard, value: hovering)
    }
}
