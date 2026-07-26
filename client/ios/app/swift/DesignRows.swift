// =============================================================================
// DesignRows.swift — các dòng/thẻ mang DỮ LIỆU của sản phẩm: thẻ máy đã nhớ và dòng
// nguồn chọn được. Đối ứng client/macos/app/swift/DesignRows.swift.
//
// BẢN MOBILE THIẾU HAI THỨ SO VỚI DESKTOP, CÓ CHỦ Ý
//   AddressRow (địa chỉ để đọc cho người khác) và PermissionBanner (quyền Screen
//   Recording / Accessibility) chỉ thuộc vai HOST. iOS là client-only — sandbox chặn
//   tuyệt đối cả việc mở cổng nghe lẫn việc bơm input sang app khác
//   (docs/11-platform-transport.md §3) — nên hai thành phần đó không có gì để hiển thị.
//
// LIÊN QUAN: DesignControls.swift (ô tick trong dòng nguồn), DesignSurfaces.swift
// =============================================================================
import SwiftUI

// MARK: - Thẻ máy

// Một máy đã từng kết nối: chấm trạng thái + tên, địa chỉ mono, chip loại đường.
//
// CẢ THẺ LÀ MỘT NÚT, và bấm vào nó chỉ ĐIỀN SẴN địa chỉ chứ không nối thẳng: nối
// thẳng sẽ bỏ qua ô "chỉ xem", mà đó là lựa chọn người ta cần cân nhắc TRƯỚC khi gõ
// vào máy người khác.
struct MachineCard: View {
    let name: String
    let address: String
    var link: String = ""
    var live = false
    let onTap: () -> Void

    var body: some View {
        Button(action: onTap) {
            HStack(spacing: 12) {
                StatusDot(live: live)
                VStack(alignment: .leading, spacing: 3) {
                    Text(name)
                        .font(DS.ui(DS.textBodyLg, weight: .semibold))
                        .foregroundStyle(DS.textPrimary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                    // Host chưa xưng tên thì `name` CHÍNH LÀ địa chỉ (RecentMachine.
                    // displayName). In lại nó lần nữa ngay bên dưới chỉ làm thẻ cao
                    // gấp đôi để nói đúng một điều.
                    if name != address {
                        MonoText(text: address)
                            .lineLimit(1)
                    }
                }
                Spacer(minLength: 8)
                // Trường rỗng thì KHÔNG vẽ gì — một chip trống là nhiễu, và loại đường
                // ta không đoán ra được thì đừng bịa.
                if !link.isEmpty { Chip(text: link) }
            }
        }
        .buttonStyle(DSCardButtonStyle())
    }
}

// MARK: - Dòng nguồn

// Một dòng "cửa sổ hoặc màn hình" trên màn chọn nguồn:
//   [tick] [biểu tượng] Tên nguồn            2560×1600  [pill trạng thái]
//
// BẤM ĐÂU TRONG DÒNG CŨNG CHỌN — khác màn CHIA SẺ bên desktop, nơi chỉ ô tick nhận
// bấm vì mỗi dòng ở đó có thể đang truyền hình cho người khác. Ở màn này chưa có
// phiên nào cả, chọn sai thì chọn lại; và trên cảm ứng, bắt ngón tay nhắm vào đúng ô
// vuông 22pt trong khi cả dòng đang trống là làm khó không đổi lấy gì.
//
// Ô TICK HÀNH XỬ NHƯ RADIO: dh_start nhận MỘT sourceId, nên chọn cái mới là bỏ cái cũ.
struct SourceRow: View {
    let name: String
    let detail: String
    let selected: Bool
    var state: String = ""
    var tone: StatePill.Tone = .neutral
    let onSelect: () -> Void

    var body: some View {
        Button(action: onSelect) {
            HStack(spacing: 12) {
                Toggle(isOn: .constant(selected)) { EmptyView() }
                    .toggleStyle(DSCheckboxStyle(decorative: true))
                    .labelsHidden()

                // Giao thức không nói nguồn là cửa sổ hay màn hình ở phía client
                // (DHSourceInfo chỉ có tên + kích thước), nên đừng đoán: một biểu
                // tượng cửa sổ dùng chung, thay vì gán bừa "màn hình" cho thứ có thể
                // là một cửa sổ.
                Image(systemName: "macwindow")
                    .font(.system(size: 16))
                    .foregroundStyle(selected ? DS.accent : DS.textSecondary)
                    .frame(width: 22)

                VStack(alignment: .leading, spacing: 3) {
                    Text(name)
                        .font(DS.ui(DS.textBodyLg, weight: .semibold))
                        .foregroundStyle(DS.textPrimary)
                        .lineLimit(1)
                        .truncationMode(.middle)
                    MonoText(text: detail)
                        .lineLimit(1)
                }

                Spacer(minLength: 8)

                if !state.isEmpty { StatePill(text: state, tone: tone) }
            }
        }
        .buttonStyle(DSCardButtonStyle(selected: selected))
    }
}
