// =============================================================================
// DesignControls.swift — những thứ NHẬN THAO TÁC gõ/tick/chọn: ô tick, ô địa chỉ
// hero, ô chọn nhỏ ở thanh dưới.
//
// VÌ SAO TỰ VIẾT THAY VÌ DÙNG Toggle/TextField/Picker MẶC ĐỊNH
//   Control mặc định của AppKit mang nguyên bộ trang trí của hệ thống (ô tick tô màu
//   nhấn của HỆ ĐIỀU HÀNH, viền focus xanh, chiều cao tối thiểu riêng) và không có
//   cách nào bỏ hết chúng mà không dựng lại control. Ở màn chia sẻ có tới sáu ô tick
//   nằm cạnh nhau nên khác biệt đó đập vào mắt ngay.
//
// LIÊN QUAN: DesignTokens.swift (bảng token), DesignRows.swift (nơi dùng ô tick)
// =============================================================================
import AppKit
import SwiftUI

// MARK: - Ô tick

// Ô 18px bo 6, tô mint khi tick, dấu tick vẽ bằng Path.
//
// VÙNG BẤM PHỦ CẢ NHÃN, không chỉ mỗi ô vuông 18px — bắt người dùng nhắm vào một ô
// 18px trong khi có sẵn một dòng chữ ngay cạnh là làm khó không đổi lấy gì.
struct DSCheckboxStyle: ToggleStyle {
    var side: CGFloat = 18
    /// Ô tick chỉ để NHÌN: cả dòng bên ngoài mới là cái nút. Để nguyên thì cú bấm
    /// trúng ô tick sẽ chạy hai lần và trạng thái quay về chỗ cũ — lỗi trông như
    /// "bấm vào ô tick thì không ăn".
    var decorative = false

    func makeBody(configuration: Configuration) -> some View {
        let box = ZStack {
            RoundedRectangle(cornerRadius: 6, style: .continuous)
                .fill(configuration.isOn ? DS.accent : DS.checkboxOff)
            RoundedRectangle(cornerRadius: 6, style: .continuous)
                .strokeBorder(configuration.isOn ? DS.accent : DS.borderHairline, lineWidth: DS.hairline)
            if configuration.isOn {
                Path { tick in
                    tick.move(to: CGPoint(x: 0, y: 4.2))
                    tick.addLine(to: CGPoint(x: 3.4, y: 7.6))
                    tick.addLine(to: CGPoint(x: 9.4, y: 0.6))
                }
                .stroke(
                    DS.textOnAccent,
                    style: StrokeStyle(lineWidth: 2.2, lineCap: .round, lineJoin: .round)
                )
                .frame(width: 9.4, height: 8)
            }
        }
        .frame(width: side, height: side)
        .animation(DS.easeStandard, value: configuration.isOn)

        let row = HStack(spacing: 9) {
            box
            configuration.label
                .font(DS.ui(DS.textBodySm))
                .foregroundStyle(DS.textPrimary)
        }

        return Group {
            if decorative {
                row
            } else {
                row
                    .contentShape(Rectangle())
                    .onTapGesture { configuration.isOn.toggle() }
            }
        }
    }
}

// MARK: - Ô nhập

// Ô địa chỉ hero: MỘT hàng có viền chứa [biểu tượng][ô nhập][nút], cao 66, bo 16.
// Nút nằm TRONG ô chứ không đứng cạnh — nó là hành động của chính ô đó, và đặt vào
// trong thì mắt đọc cả cụm là một việc duy nhất.
struct HeroField<Trailing: View>: View {
    let icon: String
    @Binding var text: String
    var placeholder: String = ""
    var onSubmit: () -> Void = {}
    @ViewBuilder var trailing: Trailing

    @FocusState private var focused: Bool

    var body: some View {
        HStack(spacing: 14) {
            Image(systemName: icon)
                .font(.system(size: 19))
                .foregroundStyle(DS.accent)
            TextField(placeholder, text: $text)
                .textFieldStyle(.plain)
                .font(DS.mono(22))
                .foregroundStyle(DS.textPrimary)
                .focused($focused)
                .onSubmit(onSubmit)
            trailing
        }
        .padding(.horizontal, 18)
        .frame(height: DS.fieldHeight)
        .background(DS.surfaceField, in: RoundedRectangle(cornerRadius: DS.radiusLg, style: .continuous))
        .overlay(
            // Viền đổi sang mint khi ô có tiêu điểm; quầng mint theo cùng. Cả hai đổi
            // trong 140ms — đường cong ease-standard, giống mọi chuyển trạng thái khác.
            RoundedRectangle(cornerRadius: DS.radiusLg, style: .continuous)
                .strokeBorder(focused ? DS.borderActive : DS.borderHairline, lineWidth: DS.hairline)
        )
        .shadow(color: focused ? DS.glowAccent : .clear, radius: 20, y: 12)
        .animation(DS.easeStandard, value: focused)
    }
}

// MARK: - Ô chọn

// Ô chọn nhỏ ở thanh dưới màn chia sẻ (fps, bitrate, cổng). Mono, cao 30, bo 10.
//
// Dùng Menu chứ không phải Picker: Picker của macOS vẽ ra một NSPopUpButton mang
// nguyên bộ trang trí của hệ thống (viền xanh khi focus, mũi tên đôi, chiều cao tối
// thiểu 22–28pt) và không có cách nào bỏ hết chúng mà không dựng lại control. Menu thì
// nhãn là view của ta hoàn toàn.
struct DSSelect: View {
    let options: [String]
    @Binding var value: String
    var width: CGFloat = 92

    @State private var hovering = false

    var body: some View {
        Menu {
            ForEach(options, id: \.self) { option in
                Button(option) { value = option }
            }
        } label: {
            HStack(spacing: 6) {
                Text(value)
                    .font(DS.mono(DS.textBodySm))
                    .foregroundStyle(DS.textPrimary)
                Spacer(minLength: 0)
                Image(systemName: "chevron.down")
                    .font(.system(size: 9, weight: .semibold))
                    .foregroundStyle(DS.textSecondary)
            }
            .padding(.horizontal, 10)
            .frame(width: width, height: DS.controlHeightSm)
            .background(
                hovering ? DS.surfaceControlHover : DS.surfaceControl,
                in: RoundedRectangle(cornerRadius: DS.radiusSm, style: .continuous)
            )
            .overlay(
                RoundedRectangle(cornerRadius: DS.radiusSm, style: .continuous)
                    .strokeBorder(DS.borderHairline, lineWidth: DS.hairline)
            )
            .contentShape(RoundedRectangle(cornerRadius: DS.radiusSm, style: .continuous))
        }
        .menuStyle(.borderlessButton)
        .menuIndicator(.hidden)
        .fixedSize()
        .onHover { hovering = $0 }
        .animation(DS.easeStandard, value: hovering)
    }
}
