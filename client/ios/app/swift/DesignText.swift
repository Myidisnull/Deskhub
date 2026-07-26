// =============================================================================
// DesignText.swift — chữ và nguồn sáng của hệ thiết kế: quầng cobalt, eyebrow, mono,
// tiêu đề màn, nhãn mục. Đây là những thứ KHÔNG nhận thao tác, chỉ nói ra thông tin.
// Đối ứng client/macos/app/swift/DesignText.swift.
//
// LIÊN QUAN: DesignTokens.swift (bảng token), DesignButtons.swift, DesignSurfaces.swift
// =============================================================================
import SwiftUI

// MARK: - Nguồn sáng

// Một quầng cobalt sau bố cục — KHÔNG BAO GIỜ là một lớp nền. Nó là "đèn" của bản
// thiết kế: một vệt sáng hắt từ ngoài khung vào, cho nền gần-đen có chiều sâu mà
// không cần đổ bóng ở đâu cả.
//
// KHÁC BẢN DESKTOP: QUẦNG NHỎ HƠN VÀ NÉP SÁT MÉP HƠN
//   Bản macOS dùng quầng 900pt lệch ra ngoài 260pt — với cửa sổ 1200pt thì đó là một
//   vệt sáng ở góc. Giữ nguyên con số ấy trên màn 390pt thì quầng phủ TOÀN BỘ màn
//   hình và biến thành lớp nền, đúng thứ bản thiết kế cấm. Kích thước ở đây tính
//   theo bề ngang màn hình để tỉ lệ "vệt sáng góc" được giữ nguyên.
struct Glow: View {
    var alignment: Alignment = .topTrailing

    var body: some View {
        GeometryReader { geo in
            let side = max(geo.size.width, 320) * 1.35
            RadialGradient(
                gradient: Gradient(stops: [
                    .init(color: DS.glowCobalt, location: 0),
                    .init(color: DS.glowCobalt.opacity(0), location: 0.62),
                ]),
                center: .center,
                startRadius: 0,
                endRadius: side / 2
            )
            .frame(width: side, height: side)
            .offset(x: side * 0.28, y: -side * 0.34)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: alignment)
        }
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

// Tiêu đề màn: eyebrow + tiêu đề phông hiển thị + chú thích mono.
//
// KHÁC BẢN DESKTOP: CHÚ THÍCH XUỐNG DÒNG THAY VÌ NẰM PHẢI
//   Desktop đặt `aside` cùng hàng với tiêu đề, sát đáy chữ. Trên màn hẹp, một địa
//   chỉ IP mono nằm cạnh tiêu đề sẽ ép tiêu đề xuống ba dòng — nên nó tụt xuống
//   dòng riêng ngay dưới, vẫn đọc theo đúng thứ tự cũ.
struct ScreenHeader: View {
    let eyebrow: String
    let title: String
    var aside: String?
    var size: CGFloat = DS.textTitle

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            Eyebrow(text: eyebrow.uppercased())
            Text(title)
                .font(DS.display(size))
                .tracking(DS.trackDisplay(size))
                .foregroundStyle(DS.textPrimary)
                .fixedSize(horizontal: false, vertical: true)
            if let aside {
                MonoText(text: aside)
                    .lineLimit(1)
                    .truncationMode(.middle)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
    }
}

// Nhãn mục + gạch tóc kéo hết chiều ngang + phần đuôi tuỳ ý. Gạch ngang là thứ giữ
// cho các mục không dính vào nhau khi một màn có hai, ba danh sách chồng lên.
struct SectionHeader<Trailing: View>: View {
    let label: String
    @ViewBuilder var trailing: Trailing

    var body: some View {
        HStack(spacing: 10) {
            Eyebrow(text: label.uppercased(), dim: true)
                .layoutPriority(1)
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
