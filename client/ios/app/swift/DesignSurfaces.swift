// =============================================================================
// DesignSurfaces.swift — bề mặt (panel, thẻ, chip, HUD) và các chỉ báo trạng thái
// (chấm sống, pill, biểu đồ độ trễ, vòng quay).
// Đối ứng client/macos/app/swift/DesignSurfaces.swift.
//
// MỌI BỀ MẶT LÀ MỘT LỚP KÍNH, KHÔNG PHẢI MÀU ĐẶC
//   Ở nền tối, card/HUD đều là một lớp trắng MỜ chồng lên nền gần-đen; chồng lớp
//   chính là thứ tạo ra chiều sâu ở đây. Chỉ HUD nổi trên video mới đổ bóng.
//
// LIÊN QUAN: DesignTokens.swift (bảng token), DesignText.swift
// =============================================================================
import SwiftUI

// MARK: - Bề mặt

// Panel: hộp kính bo 18, dùng cho mọi khối nội dung có nhãn.
struct Panel<Content: View>: View {
    let label: String
    @ViewBuilder var content: Content

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Eyebrow(text: label.uppercased(), dim: true)
            content
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(16)
        .background(DS.surfaceCard, in: RoundedRectangle(cornerRadius: DS.radiusXl, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: DS.radiusXl, style: .continuous)
                .strokeBorder(DS.borderHairline, lineWidth: DS.hairline)
        )
    }
}

// Card một dòng trong danh sách (máy, nguồn, địa chỉ). Bo 16, thấp hơn panel.
struct RowCard<Content: View>: View {
    var radius: CGFloat = DS.radiusLg
    var padding: EdgeInsets = .init(top: 12, leading: 14, bottom: 12, trailing: 14)
    @ViewBuilder var content: Content

    var body: some View {
        content
            .padding(padding)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(DS.surfaceCard, in: RoundedRectangle(cornerRadius: radius, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: radius, style: .continuous)
                    .strokeBorder(DS.borderHairline, lineWidth: DS.hairline)
            )
    }
}

// Chip: nhãn mono nhỏ, bo pill, CÓ viền tóc — siêu dữ liệu tĩnh.
struct Chip: View {
    let text: String
    var active = false

    var body: some View {
        Text(text)
            .font(DS.mono(DS.textMonoSm))
            .foregroundStyle(active ? DS.accent : DS.textSecondary)
            .padding(.horizontal, 10)
            .padding(.vertical, 4)
            .background(
                active ? DS.accentWash : DS.surfaceControl,
                in: RoundedRectangle(cornerRadius: DS.radiusPill, style: .continuous)
            )
            .overlay(
                RoundedRectangle(cornerRadius: DS.radiusPill, style: .continuous)
                    .strokeBorder(active ? DS.borderActive : DS.borderHairline, lineWidth: DS.hairline)
            )
    }
}

// HUD: thanh nổi trên video. Đây là thứ DUY NHẤT đổ bóng trong bản mobile.
struct HudBar<Content: View>: View {
    var spacing: CGFloat = 8
    @ViewBuilder var content: Content

    var body: some View {
        HStack(spacing: spacing) {
            content
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 7)
        .background(
            DS.bgChrome,
            in: RoundedRectangle(cornerRadius: DS.radiusPill, style: .continuous)
        )
        .background(
            .ultraThinMaterial,
            in: RoundedRectangle(cornerRadius: DS.radiusPill, style: .continuous)
        )
        .overlay(
            RoundedRectangle(cornerRadius: DS.radiusPill, style: .continuous)
                .strokeBorder(DS.borderHairline, lineWidth: DS.hairline)
        )
        .shadow(color: .black.opacity(0.5), radius: 18, y: 10)
    }
}

struct HudDivider: View {
    var body: some View {
        Rectangle()
            .fill(DS.borderHairline)
            .frame(width: DS.hairline, height: 16)
    }
}

// MARK: - Trạng thái

// Chấm 8px "đang sống / không". Nhỏ nhất nhưng xuất hiện nhiều nhất trong bản thiết
// kế: mỗi thẻ máy, mỗi dòng nguồn, mỗi HUD đều có một cái.
//
// CHẤM SỐNG CÓ QUẦNG SÁNG, CHẤM CHẾT THÌ KHÔNG. Đây không phải trang trí: cả hai
// trạng thái đều là hình tròn cùng cỡ, chỉ khác màu thì người mù màu xanh–đỏ (khoảng
// 8% nam giới) không phân biệt được. Quầng thêm một tín hiệu thứ hai — kích thước và
// độ chói — mà ai cũng thấy.
struct StatusDot: View {
    var live: Bool

    var body: some View {
        ZStack {
            Circle()
                .fill(DS.accentWash)
                .frame(width: 16, height: 16)
                .opacity(live ? 1 : 0)
            Circle()
                .fill(live ? DS.statusLive : DS.statusIdle)
                .frame(width: 8, height: 8)
                .opacity(live ? 1 : 0.55)
        }
        .frame(width: 16, height: 16)
    }
}

// Nhãn trạng thái mono viết HOA của một nguồn hoặc một phiên.
// BA TÔNG, KHÔNG PHẢI BA MÀU TUỲ Ý: sống = mint, trung tính = xám trên kính,
// đứt = đỏ. Đúng ba trạng thái mà sản phẩm này có.
//
// Pill KHÔNG VIỀN, khác Chip (có viền tóc): chip là siêu dữ liệu tĩnh, pill là trạng
// thái đang chạy — nền rửa màu chính là thứ phân biệt hai loại đó từ xa.
struct StatePill: View {
    enum Tone { case live, neutral, danger }

    let text: String
    var tone: Tone = .neutral

    var body: some View {
        Text(text.uppercased())
            .font(DS.mono(DS.textLabel))
            // Giãn chữ .1em, KHÔNG phải .2em của eyebrow: pill là một từ đứng trong
            // hộp hẹp, giãn bằng eyebrow thì chữ chạm mép bo.
            .tracking(0.1 * DS.textLabel)
            .foregroundStyle(foreground)
            .padding(.horizontal, 10)
            .padding(.vertical, 4)
            .background(background, in: RoundedRectangle(cornerRadius: DS.radiusPill, style: .continuous))
    }

    private var foreground: Color {
        switch tone {
        case .live: DS.statusLive
        case .neutral: DS.textSecondary
        case .danger: DS.statusError
        }
    }

    private var background: Color {
        switch tone {
        case .live: DS.accentWash
        case .neutral: DS.surfaceControl
        case .danger: DS.dangerWash
        }
    }
}

// Nhãn nhỏ ở trên, con số to ở dưới, đơn vị nhạt bên cạnh.
//
// Con số dùng phông HIỂN THỊ chứ không phải mono, dù mọi số khác trong app đều mono:
// đây là số để LIẾC, không phải để đọc từng chữ.
struct StatBlock: View {
    let label: String
    let value: String
    var unit: String = ""
    var accent = false

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            Eyebrow(text: label.uppercased(), dim: true)
            HStack(alignment: .lastTextBaseline, spacing: 5) {
                Text(value)
                    .font(DS.display(DS.textStat))
                    .tracking(DS.trackTitle(DS.textStat))
                    .foregroundStyle(accent ? DS.accent : DS.textPrimary)
                if !unit.isEmpty {
                    MonoText(text: unit)
                }
            }
        }
    }
}

// Biểu đồ đường độ trễ. Một đường mint mảnh trên nền trong suốt: không trục, không
// lưới, không nhãn.
//
// THANG ĐỨNG TỰ ĐỘNG THEO DỮ LIỆU ĐANG HIỆN — không neo đáy vào 0. Độ trễ dao động
// 4–7 ms trên thang 0–7 gần như là một đường thẳng, mà chính cái dao động đó là thứ
// người dùng nhìn biểu đồ để xem.
struct Sparkline: View {
    /// Dãy mẫu theo thứ tự CŨ → MỚI, vẽ trái sang phải.
    let values: [Double]

    var body: some View {
        Canvas { context, size in
            guard values.count >= 2, size.width > 1, size.height > 1 else { return }
            let lo = values.min() ?? 0
            let hi = values.max() ?? 0
            let span = hi - lo
            var path = Path()

            // Dãy phẳng hoàn toàn: span = 0 sẽ chia cho 0. Vẽ nó thành một đường ngang
            // giữa khung — đó đúng là sự thật của dữ liệu.
            guard span > 1e-6 else {
                path.move(to: CGPoint(x: 0, y: size.height / 2))
                path.addLine(to: CGPoint(x: size.width, y: size.height / 2))
                context.stroke(path, with: .color(DS.accent), lineWidth: 1.5)
                return
            }

            let pad = 0.12 // chừa mép trên/dưới để đường không dán vào viền
            let usable = size.height * (1 - 2 * pad)
            let step = size.width / CGFloat(values.count - 1)
            for (idx, sample) in values.enumerated() {
                let norm = (sample - lo) / span
                // Trục y của màn hình hướng XUỐNG, nên mẫu lớn nhất phải nằm ở trên.
                let posY = size.height * pad + (1 - norm) * usable
                let point = CGPoint(x: CGFloat(idx) * step, y: posY)
                if idx == 0 { path.move(to: point) } else { path.addLine(to: point) }
            }
            context.stroke(
                path,
                with: .color(DS.accent),
                style: StrokeStyle(lineWidth: 1.5, lineCap: .round, lineJoin: .round)
            )
        }
    }
}

// Vòng quay. Cùng với biểu đồ độ trễ, đây là hai chuyển động liên tục DUY NHẤT trong
// sản phẩm (tokens/motion.css).
struct Spinner: View {
    var size: CGFloat = 16
    @State private var spinning = false

    var body: some View {
        Circle()
            .trim(from: 0, to: 0.72)
            .stroke(DS.accent, style: StrokeStyle(lineWidth: 1.8, lineCap: .round))
            .frame(width: size, height: size)
            .rotationEffect(.degrees(spinning ? 360 : 0))
            .animation(.linear(duration: 0.9).repeatForever(autoreverses: false), value: spinning)
            .onAppear { spinning = true }
    }
}
