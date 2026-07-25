// =============================================================================
// DesignText.swift — chữ và nguồn sáng của hệ thiết kế: quầng cobalt, eyebrow, mono,
// tiêu đề màn, nhãn mục. Đây là những thứ KHÔNG nhận thao tác, chỉ nói ra thông tin.
//
// LIÊN QUAN: DesignTokens.swift (bảng token), DesignButtons.swift, DesignSurfaces.swift
// =============================================================================
import AppKit
import SwiftUI

// MARK: - Nguồn sáng

// Một quầng cobalt sau bố cục — KHÔNG BAO GIỜ là một lớp nền. Nó là "đèn" của bản
// thiết kế: một vệt sáng hắt từ ngoài khung vào, cho nền gần-đen có chiều sâu mà
// không cần đổ bóng ở đâu cả. Kéo nó vào giữa màn hình sẽ thành một quầng trang trí,
// khác hẳn về cảm giác.
struct Glow: View {
    var size: CGFloat = 900
    var alignment: Alignment = .topTrailing
    var offset: CGSize = .init(width: 260, height: -320)

    var body: some View {
        RadialGradient(
            gradient: Gradient(stops: [
                .init(color: DS.glowCobalt, location: 0),
                .init(color: DS.glowCobalt.opacity(0), location: 0.62),
            ]),
            center: .center,
            startRadius: 0,
            endRadius: size / 2
        )
        .frame(width: size, height: size)
        .offset(offset)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: alignment)
        .allowsHitTesting(false)
    }
}

// MARK: - Chữ

// Eyebrow: mono, HOA, giãn chữ .2em. Nhãn nhỏ trên mỗi tiêu đề mục.
struct Eyebrow: View {
    let text: String
    var dim = false

    var body: some View {
        Text(text)
            .font(DS.mono(DS.textLabel, weight: .medium))
            .tracking(DS.trackLabel(DS.textLabel))
            .foregroundStyle(dim ? DS.textSecondary : DS.accent)
    }
}

// Mono: MỌI con số sản phẩm hiện ra đều dùng kiểu này.
struct MonoText: View {
    let text: String
    var size: CGFloat = DS.textMonoSm
    var color: Color = DS.textSecondary

    var body: some View {
        Text(text)
            .font(DS.mono(size))
            .foregroundStyle(color)
    }
}

// Tiêu đề màn: eyebrow + tiêu đề phông hiển thị + chú thích mono nằm phải, sát đáy.
struct ScreenHeader: View {
    let eyebrow: String
    let title: String
    var aside: String?
    var size: CGFloat = DS.textTitle

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Eyebrow(text: eyebrow.uppercased())
            HStack(alignment: .lastTextBaseline) {
                Text(title)
                    .font(DS.display(size))
                    // Giãn chữ GIỮ NGUYÊN -.035em ở cả hai cỡ (title và display):
                    // ScreenHeader của bản thiết kế dùng chung `tracking-display`, chỉ
                    // đổi mỗi font-size.
                    .tracking(DS.trackDisplay(size))
                    .foregroundStyle(DS.textPrimary)
                    .fixedSize(horizontal: false, vertical: true)
                Spacer(minLength: 24)
                if let aside {
                    MonoText(text: aside)
                }
            }
        }
    }
}

// Nhãn mục + gạch tóc kéo hết chiều ngang + phần đuôi tuỳ ý. Xuất hiện trên mọi danh
// sách trong bản thiết kế; gạch ngang là thứ giữ cho các mục không dính vào nhau khi
// một màn có ba, bốn danh sách chồng lên.
struct SectionHeader<Trailing: View>: View {
    let label: String
    @ViewBuilder var trailing: Trailing

    var body: some View {
        HStack(spacing: 12) {
            Eyebrow(text: label.uppercased(), dim: true)
            Rectangle()
                .fill(DS.borderHairline)
                .frame(height: DS.hairline)
            trailing
        }
    }
}

extension SectionHeader where Trailing == EmptyView {
    init(label: String) {
        self.init(label: label) { EmptyView() }
    }
}
