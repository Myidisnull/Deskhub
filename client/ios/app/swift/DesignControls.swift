// =============================================================================
// DesignControls.swift — những thứ NHẬN THAO TÁC gõ/tick: ô tick và ô địa chỉ hero.
// Đối ứng client/macos/app/swift/DesignControls.swift.
//
// VÌ SAO TỰ VIẾT THAY VÌ DÙNG Toggle/TextField MẶC ĐỊNH
//   Control mặc định của UIKit mang nguyên bộ trang trí của hệ thống (công tắc bo
//   tròn tô màu tint của HỆ, viền ô nhập kiểu roundedBorder) và không có cách nào bỏ
//   hết chúng mà không dựng lại control. Một công tắc xanh dương kiểu iOS đặt cạnh
//   nút mint của bản thiết kế đọc ra ngay là hai app khác nhau ghép lại.
//
// LIÊN QUAN: DesignTokens.swift (bảng token), ConnectView.swift (nơi dùng cả hai)
// =============================================================================
import SwiftUI

// MARK: - Ô tick

// Ô 22px bo 6, tô mint khi tick, dấu tick vẽ bằng Path.
//
// TO HƠN BẢN DESKTOP (18 → 22) VÀ VÙNG BẤM PHỦ CẢ NHÃN
//   18pt là vùng chạm chưa tới một nửa ngưỡng 44pt của HIG. Ô vuông to lên 22 cho dễ
//   NHÌN, còn phần dễ BẤM đến từ chỗ khác: cả dòng — ô vuông lẫn nhãn — là vùng chạm,
//   và nó cao đúng 44.
struct DSCheckboxStyle: ToggleStyle {
    var side: CGFloat = 22
    /// Ô tick chỉ để NHÌN: cả dòng bên ngoài mới là cái nút. Để nguyên thì cú bấm
    /// trúng ô tick sẽ chạy hai lần và trạng thái quay về chỗ cũ — lỗi trông như
    /// "bấm vào ô tick thì không ăn".
    var decorative = false

    func makeBody(configuration: Configuration) -> some View {
        let box = ZStack {
            RoundedRectangle(cornerRadius: 7, style: .continuous)
                .fill(configuration.isOn ? DS.accent : DS.checkboxOff)
            RoundedRectangle(cornerRadius: 7, style: .continuous)
                .strokeBorder(configuration.isOn ? DS.accent : DS.borderHairline, lineWidth: DS.hairline)
            if configuration.isOn {
                Path { tick in
                    tick.move(to: CGPoint(x: 0, y: 5.2))
                    tick.addLine(to: CGPoint(x: 4.2, y: 9.4))
                    tick.addLine(to: CGPoint(x: 11.6, y: 0.8))
                }
                .stroke(
                    DS.textOnAccent,
                    style: StrokeStyle(lineWidth: 2.4, lineCap: .round, lineJoin: .round)
                )
                .frame(width: 11.6, height: 10)
            }
        }
        .frame(width: side, height: side)
        .animation(DS.easeStandard, value: configuration.isOn)

        let row = HStack(spacing: 10) {
            box
            configuration.label
                .font(DS.ui(DS.textBodySm))
                .foregroundStyle(DS.textPrimary)
                .fixedSize(horizontal: false, vertical: true)
        }
        .frame(minHeight: DS.controlHeight, alignment: .leading)

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

// Ô địa chỉ hero: MỘT hàng có viền chứa [biểu tượng][ô nhập][đuôi], cao 58, bo 16.
//
// KHÁC BẢN DESKTOP: ĐUÔI Ô KHÔNG PHẢI CHỖ ĐẶT NÚT "KẾT NỐI"
//   macOS nhét nguyên nút Connect vào trong ô. Trên màn 390pt thì nút đó ăn mất một
//   phần ba bề ngang của đúng thứ người dùng đang gõ vào, và nó lại nằm ở nửa TRÊN
//   màn hình — xa ngón cái nhất. Nút thật chuyển xuống thanh đáy (xem ScreenBody), và
//   chỗ này để dành cho vòng quay lúc đang hỏi host: ô đang bận thì nói ngay tại ô.
//
// keyboardType: numbersAndPunctuation chứ KHÔNG phải decimalPad — decimalPad hiện dấu
// thập phân theo locale (máy tiếng Việt ra "," — không gõ nổi IP) và không có phím
// Return nên onSubmit không bao giờ chạy.
struct HeroField<Trailing: View>: View {
    let icon: String
    @Binding var text: String
    var placeholder: String = ""
    var onSubmit: () -> Void = {}
    @ViewBuilder var trailing: Trailing

    @FocusState private var focused: Bool

    var body: some View {
        HStack(spacing: 12) {
            Image(systemName: icon)
                .font(.system(size: 18))
                .foregroundStyle(DS.accent)
            TextField(placeholder, text: $text)
                .textFieldStyle(.plain)
                .font(DS.mono(17))
                .foregroundStyle(DS.textPrimary)
                .tint(DS.accent)
                .keyboardType(.numbersAndPunctuation)
                .autocorrectionDisabled()
                .textInputAutocapitalization(.never)
                .submitLabel(.go)
                .focused($focused)
                .onSubmit(onSubmit)
            trailing
        }
        .padding(.horizontal, 16)
        .frame(height: DS.fieldHeight)
        .background(DS.surfaceField, in: RoundedRectangle(cornerRadius: DS.radiusLg, style: .continuous))
        .overlay(
            // Viền đổi sang mint khi ô có tiêu điểm; quầng mint theo cùng. Cả hai đổi
            // trong 140ms — đường cong ease-standard, giống mọi chuyển trạng thái khác.
            RoundedRectangle(cornerRadius: DS.radiusLg, style: .continuous)
                .strokeBorder(focused ? DS.borderActive : DS.borderHairline, lineWidth: DS.hairline)
        )
        .shadow(color: focused ? DS.glowAccent : .clear, radius: 18, y: 10)
        .animation(DS.easeStandard, value: focused)
    }
}
