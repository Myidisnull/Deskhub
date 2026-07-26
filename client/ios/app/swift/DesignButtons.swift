// =============================================================================
// DesignButtons.swift — bốn biến thể nút, nút biểu tượng, và thẻ bấm được.
// Đối ứng client/macos/app/swift/DesignButtons.swift.
//
// KHÔNG CÓ TRẠNG THÁI "RÊ CHUỘT" — VÀ ĐÓ LÀ THAY ĐỔI LỚN NHẤT SO VỚI DESKTOP
//   Bản desktop có ba bậc cho mỗi nút: nghỉ → rê → nhấn, trong đó bậc RÊ làm gần hết
//   việc nói "cái này bấm được". Cảm ứng không có con trỏ nên bậc đó không tồn tại;
//   nếu chỉ xoá nó đi thì nút mất hẳn phản hồi cho tới lúc ngón tay chạm vào. Vì vậy
//   bậc NHẤN ở đây đi thẳng tới màu đích của bậc nhấn desktop (không dừng ở màu rê),
//   và trạng thái "bấm được" phải do chính hình dáng lúc NGHỈ nói ra — viền tóc,
//   nền kính, chữ mint — chứ không chờ một thao tác nào cả.
//
// TRẠNG THÁI LÀ ĐỔI MÀU, KHÔNG PHẢI ĐỔI ĐỘ MỜ
//   Làm mờ cả nút đi kéo theo cả chữ lẫn viền, nên ô kính bị nhấn trông TỐI đi trong
//   khi bản thiết kế cho nó SÁNG lên. Chỉ trạng thái TẮT mới dùng độ mờ.
//
// VÌ SAO KHÔNG DÙNG .borderedProminent CỦA HỆ
//   Nút mặc định của UIKit lấy tint màu của HỆ (xanh dương). Cả hệ thiết kế này xoay
//   quanh đúng MỘT màu tín hiệu là mint; để lọt màu nhấn hệ thống vào là mất luôn ý
//   nghĩa của nó.
//
// LIÊN QUAN: DesignTokens.swift (bảng token), DesignSurfaces.swift
// =============================================================================
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
        case .md: 18
        case .lg: 24
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
    /// Chỉ dùng cho việc CẮT ĐỨT (Ngắt, Kết thúc).
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
                .shadow(color: glowColor, radius: 18, y: 10)
                .opacity(dimmed)
                .contentShape(RoundedRectangle(cornerRadius: radius, style: .continuous))
                .animation(DS.easeStandard, value: pressed)
        }

        private var glowColor: Color {
            guard variant == .primary, enabled else { return .clear }
            return DS.glowAccent
        }

        // Nút chính bị TẮT thì KHÔNG mờ đi mà đổi hẳn sang nền xám: một nút mint mờ
        // vẫn đọc là "mint", tức là vẫn mời bấm.
        private var dimmed: Double {
            if !enabled { return variant == .primary ? 1 : 0.45 }
            // Danger không sáng lên khi bị nhấn — nút cắt đứt phiên không nên có thêm
            // bất cứ thứ gì mời gọi bấm vào; nó chỉ nhạt đi một chút.
            if variant == .danger, pressed { return 0.8 }
            return 1
        }

        private var background: Color {
            switch variant {
            case .primary:
                if !enabled { return DS.accentDisabled }
                return pressed ? DS.accentPress : DS.accent
            case .secondary:
                return pressed ? DS.surfaceControlPress : DS.surfaceControl
            case .ghost:
                return pressed ? DS.surfaceControl : .clear
            case .danger:
                return DS.dangerWash
            }
        }

        private var border: Color {
            switch variant {
            case .primary:
                if !enabled { return .clear }
                return pressed ? DS.accentPress : DS.accent
            case .secondary: return DS.borderHairline
            case .ghost: return .clear
            case .danger: return DS.dangerLine
            }
        }

        private var foreground: Color {
            switch variant {
            case .primary: enabled ? DS.textOnAccent : DS.textSecondary
            case .secondary: DS.textPrimary
            case .ghost: pressed ? DS.accent : DS.textSecondary
            case .danger: DS.statusError
            }
        }
    }
}

// Nút chỉ có biểu tượng — dùng ở thanh trên cùng và HUD của màn xem.
//
// KHÁC BẢN DESKTOP: LÚC NGHỈ VẪN CÓ VIỀN
//   Bản macOS để nút biểu tượng trong suốt hoàn toàn khi nghỉ và chỉ hiện ô kính lúc
//   rê chuột — được, vì ở đó luôn có con trỏ đi dò đường. Trên cảm ứng thì không có
//   gì dò cả: một biểu tượng trần không nói được nó bấm được hay chỉ là hình trang
//   trí. Nên ở đây ô kính + viền tóc hiện SẴN.
struct DSIconButtonStyle: ButtonStyle {
    var side: CGFloat = DS.iconButton
    var radius: CGFloat = DS.radiusMd
    /// Mục đang bật: rửa mint + viền mint + biểu tượng mint.
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
                .animation(DS.easeStandard, value: configuration.isPressed)
        }

        private var background: Color {
            if active { return DS.accentWash }
            return configuration.isPressed ? DS.surfaceControlPress : DS.surfaceControl
        }

        private var border: Color {
            active ? DS.borderActive : DS.borderHairline
        }

        private var foreground: Color {
            if active { return DS.accent }
            return configuration.isPressed ? DS.accent : DS.textPrimary
        }
    }
}

// Thẻ bấm được (thẻ máy, dòng nguồn chọn được): nền kính nâng một nấc VÀ viền đổi
// sang mint khi bị nhấn. Viền mint là thứ nói "bấm được vào đây".
struct DSCardButtonStyle: ButtonStyle {
    var radius: CGFloat = DS.radiusLg
    var padding: EdgeInsets = .init(top: 14, leading: 14, bottom: 14, trailing: 14)
    /// Thẻ ĐANG được chọn: giữ nguyên viền mint kể cả khi không ai chạm vào.
    var selected = false

    func makeBody(configuration: Configuration) -> some View {
        StyledLabel(configuration: configuration, radius: radius, padding: padding, selected: selected)
    }

    private struct StyledLabel: View {
        let configuration: Configuration
        let radius: CGFloat
        let padding: EdgeInsets
        let selected: Bool

        @Environment(\.isEnabled) private var enabled

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
                .animation(DS.easeStandard, value: configuration.isPressed)
                .animation(DS.easeStandard, value: selected)
        }

        private var background: Color {
            if configuration.isPressed { return DS.surfaceCardPress }
            return selected ? DS.accentWash : DS.surfaceCard
        }

        private var border: Color {
            selected || configuration.isPressed ? DS.borderActive : DS.borderHairline
        }
    }
}
