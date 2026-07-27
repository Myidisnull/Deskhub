// =============================================================================
// DesignRows.swift — các dòng/thẻ mang DỮ LIỆU của sản phẩm: thẻ máy, dòng nguồn,
// dòng địa chỉ, banner quyền.
//
// LIÊN QUAN: DesignControls.swift (ô tick bên trong dòng nguồn),
//            DesignSurfaces.swift (RowCard, StatePill), DesignTokens.swift
// =============================================================================
import AppKit
import SwiftUI

// MARK: - Thẻ máy

// Hai bố cục, đúng như `MachineCard` của bản thiết kế:
//   stacked — tên trên, địa chỉ dưới, chip + độ trễ ở dưới. Dùng cho "gần đây": ít
//             dòng, mỗi dòng đáng một ô rộng rãi.
//   row     — tất cả trên một hàng. Dùng cho danh sách dài và đổi liên tục.
struct MachineCard: View {
    enum Layout { case stacked, row }

    let name: String
    let address: String
    var link: String = ""
    var rtt: String = ""
    var live = false
    var layout: Layout = .stacked
    let onTap: () -> Void

    var body: some View {
        Button(action: onTap) {
            switch layout {
            case .stacked: stacked
            case .row: row
            }
        }
        .buttonStyle(DSCardButtonStyle())
    }

    private var stacked: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 8) {
                StatusDot(live: live)
                Text(name)
                    .font(DS.ui(DS.textBodyLg, weight: .semibold))
                    .foregroundStyle(DS.textPrimary)
                    .lineLimit(1)
                    .truncationMode(.tail)
            }
            MonoText(text: address)
                .lineLimit(1)
            meta
        }
    }

    private var row: some View {
        HStack(spacing: 12) {
            StatusDot(live: live)
            VStack(alignment: .leading, spacing: 3) {
                Text(name)
                    .font(DS.ui(DS.textBodyLg, weight: .semibold))
                    .foregroundStyle(DS.textPrimary)
                    .lineLimit(1)
                MonoText(text: address)
                    .lineLimit(1)
            }
            Spacer(minLength: 12)
            meta
        }
    }

    // Trường rỗng thì KHÔNG vẽ gì — một chip trống hay chữ "— ms" đều là nhiễu, và độ
    // trễ ta chưa đo thì đừng bịa ra.
    private var meta: some View {
        HStack(spacing: 8) {
            if !link.isEmpty { Chip(text: link) }
            if !rtt.isEmpty { MonoText(text: rtt) }
        }
    }
}

// MARK: - Dòng nguồn

// Một dòng màn hình:
//   [tick] [biểu tượng] Tên nguồn            2560×1600  [pill trạng thái] [Dừng]
//
// BẤM CẢ DÒNG HAY CHỈ BẤM Ô TICK — TUỲ MÀN, VÀ RANH GIỚI LÀ "CÓ ĐANG CHẠY KHÔNG"
//   Màn CHIA SẺ (rowToggles: false): chỉ ô tick nhận bấm. Dòng ở đó có thể đang truyền
//   hình cho người khác, và bấm nhầm vào tên nguồn mà cắt luôn phiên của họ là một cú
//   bấm rất đắt để hoàn tác.
//   Màn CHỌN NGUỒN (rowToggles: true): bấm đâu trong dòng cũng chọn. Ở đó chưa có
//   phiên nào cả, chọn sai thì chọn lại.
struct SourceRow: View {
    let name: String
    let detail: String
    let selected: Bool
    var state: String = ""
    var tone: StatePill.Tone = .neutral
    var rowToggles = false
    var onToggle: (Bool) -> Void
    var stopLabel: String?
    var onStop: (() -> Void)?

    var body: some View {
        if rowToggles {
            Button { onToggle(!selected) } label: { content }
                .buttonStyle(DSCardButtonStyle(radius: DS.radiusLg))
        } else {
            RowCard { content }
        }
    }

    private var content: some View {
        HStack(spacing: 12) {
            // Bọc onToggle trong closure chứ không truyền thẳng: Binding.set nhận một
            // hàm @Sendable, còn onToggle đến từ view nên nó bị ràng vào MainActor.
            Toggle(isOn: Binding(get: { selected }, set: { onToggle($0) })) { EmptyView() }
                .toggleStyle(DSCheckboxStyle(decorative: rowToggles))
                .labelsHidden()

            Image(systemName: "display")
                .font(.system(size: 16))
                .foregroundStyle(DS.textSecondary)
                .frame(width: 20)

            VStack(alignment: .leading, spacing: 3) {
                Text(name)
                    .font(DS.ui(DS.textBodyLg, weight: .semibold))
                    .foregroundStyle(DS.textPrimary)
                    .lineLimit(1)
                    .truncationMode(.middle)
                MonoText(text: detail)
                    .lineLimit(1)
            }

            Spacer(minLength: 12)

            HStack(spacing: 10) {
                if !state.isEmpty { StatePill(text: state, tone: tone) }
                // Nút "Dừng" chỉ hiện ở nguồn ĐANG truyền: dừng một thứ chưa chạy là
                // câu vô nghĩa. Bỏ tick cũng ra cùng kết quả, nhưng giữa lúc đang chia
                // sẻ thì "dừng cái này" là một câu lệnh còn "bỏ tick" là thao tác
                // chỉnh danh sách — người đang vội cần thấy đúng cái nút mang chữ đó.
                if let stopLabel, let onStop {
                    Button(stopLabel, action: onStop)
                        .buttonStyle(DSButtonStyle(variant: .secondary, size: .sm))
                }
            }
        }
    }
}

// MARK: - Dòng địa chỉ

// "192.168.1.7:47777   WI-FI   [chép]".
//
// CỠ CHỮ LỚN HƠN MỌI CHỖ KHÁC LÀ CÓ CHỦ Ý: con số này thường được đọc TO cho người ở
// phòng khác, hoặc gõ lại bằng tay trên điện thoại. Nó không phải siêu dữ liệu; ở màn
// chia sẻ nó gần như là nội dung chính. Bản thiết kế đặt riêng text-mono: 16px cho
// panel này.
//
// NÚT CHÉP LUÔN HIỆN, không phải chỉ khi rê chuột: nút hiện-khi-hover thì không ai
// biết là có, và ở đây nó là đường tắt duy nhất thay cho việc chép tay một chuỗi số.
struct AddressRow: View {
    let address: String
    let link: String

    @State private var copied = false

    var body: some View {
        RowCard(radius: DS.radiusMd, padding: .init(top: 10, leading: 16, bottom: 10, trailing: 10)) {
            HStack(spacing: 12) {
                Text(address)
                    .font(DS.mono(16))
                    .foregroundStyle(DS.textPrimary)
                    .textSelection(.enabled)
                    .lineLimit(1)
                Spacer(minLength: 8)
                // Nhãn đường truyền là CHỮ, không phải chip: chip đánh dấu một trạng
                // thái, còn "Wi-Fi" chỉ nói địa chỉ này thuộc card mạng nào.
                if !link.isEmpty {
                    Eyebrow(text: link.uppercased(), dim: true)
                }
                Button {
                    NSPasteboard.general.clearContents()
                    NSPasteboard.general.setString(address, forType: .string)
                    copied = true
                    Task {
                        try? await Task.sleep(for: .seconds(1.4))
                        copied = false
                    }
                } label: {
                    Image(systemName: copied ? "checkmark" : "doc.on.doc")
                        .font(.system(size: 14))
                }
                .buttonStyle(DSButtonStyle(variant: .ghostAccent, size: .sm))
                .help(tr("copy"))
            }
        }
    }
}

// MARK: - Banner quyền

// macOS-only: thiếu quyền là HỎNG IM LẶNG (danh sách nguồn gần như trống, input bị bỏ
// mà không báo gì). Banner nằm TRÊN CÙNG màn chia sẻ vì không có nó thì người dùng
// không thể đoán vì sao mọi thứ trống rỗng.
struct PermissionBanner: View {
    let title: String
    let detail: String
    let actionLabel: String
    let onAction: () -> Void

    var body: some View {
        HStack(alignment: .top, spacing: 12) {
            Image(systemName: "exclamationmark.triangle.fill")
                .font(.system(size: 15))
                .foregroundStyle(DS.statusWarn)
            VStack(alignment: .leading, spacing: 4) {
                Text(title)
                    .font(DS.ui(DS.textBody, weight: .semibold))
                    .foregroundStyle(DS.textPrimary)
                Text(detail)
                    .font(DS.ui(DS.textBodySm))
                    .foregroundStyle(DS.textSecondary)
                    .fixedSize(horizontal: false, vertical: true)
            }
            Spacer(minLength: 12)
            Button(actionLabel, action: onAction)
                .buttonStyle(DSButtonStyle(variant: .secondary, size: .sm))
        }
        .padding(14)
        .background(DS.bannerWarn, in: RoundedRectangle(cornerRadius: DS.radiusLg, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: DS.radiusLg, style: .continuous)
                .strokeBorder(DS.statusWarn.opacity(0.28), lineWidth: DS.hairline)
        )
    }
}
