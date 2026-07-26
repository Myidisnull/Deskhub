// =============================================================================
// SourcePickerView.swift — màn "Chọn thứ muốn xem" (vai CLIENT).
//                          Dựng theo `DesktopPick` trong desktop.jsx; đối ứng
//                          client/macos/app/swift/SourcePickerView.swift và
//                          SourcePickerScreen bên Android.
//
// MÀN NÀY XEN GIỮA CONNECT VÀ VIEWER
//   Host chia sẻ nhiều cửa sổ/màn hình cùng lúc, mỗi cái một sourceId. Không có màn
//   này thì client luôn xem nguồn 0 và không có đường nào tới các nguồn còn lại.
//
// MỘT LẦN MỘT NGUỒN
//   dh_start nhận MỘT sourceId, nên ô tick hành xử như radio: chọn cái mới là bỏ cái
//   cũ. Bản thiết kế nói đúng điều đó trong chính chuỗi `pickHint`.
//
// HOST IM LẶNG KHÔNG PHẢI LÀ LỖI
//   Host đời trước GĐ6 không biết LIST_SOURCES. Lúc đó màn này KHÔNG hiện ra — model
//   đi thẳng sang màn xem với nguồn 0, đúng hành vi cũ. Người dùng không được thấy
//   một màn trống và một lời báo lỗi cho chuyện họ không làm gì sai.
//
// KHÔNG DÙNG List/NavigationStack CỦA HỆ
//   Bản cũ dùng List + navigationTitle, và cả hai mang nguyên bộ trang trí iOS: nền
//   xám hệ thống, đường kẻ inset, tiêu đề phông hệ. Chúng kéo màn này ra khỏi hệ
//   thiết kế mạnh hơn bất cứ thứ gì khác trên màn hình.
// =============================================================================
import SwiftUI

struct SourcePickerView: View {
    @Bindable var model: SessionModel
    let sources: [Source]

    @State private var picked: UInt8?

    private var selectedId: UInt8 { picked ?? sources.first?.id ?? 0 }

    var body: some View {
        ScreenBody {
            ScreenHeader(
                eyebrow: tr("clientMode"),
                title: tr("pickTitle"),
                aside: model.address
            )

            VStack(alignment: .leading, spacing: 10) {
                SectionHeader(label: tr("sharedByHost")) {
                    StatusDot(live: !sources.isEmpty)
                    MonoText(text: String(
                        format: tr("publishedCountFmt"),
                        sources.count,
                        sources.isEmpty ? 0 : 1
                    ))
                }

                if sources.isEmpty {
                    MonoText(text: tr("noSourcesShared"))
                } else {
                    ForEach(sources) { source in
                        SourceRow(
                            name: source.name.isEmpty
                                ? String(format: tr("unnamedSourceFmt"), Int(source.id))
                                : source.name,
                            detail: "\(source.width)×\(source.height)",
                            selected: source.id == selectedId,
                            state: source.id == selectedId ? tr("streaming") : tr("idle"),
                            tone: source.id == selectedId ? .live : .neutral
                        ) { picked = source.id }
                    }
                }

                MonoText(text: tr("pickHint"))
                    .fixedSize(horizontal: false, vertical: true)
            }
        } bar: {
            Toggle(isOn: Binding(get: { !model.viewOnly }, set: { model.viewOnly = !$0 })) {
                Text(tr("allowInput"))
            }
            .toggleStyle(DSCheckboxStyle())

            Button {
                model.startStream(sourceId: selectedId)
            } label: {
                Label(tr("startViewing"), systemImage: "play.rectangle")
            }
            .buttonStyle(DSButtonStyle(variant: .primary, size: .lg, fullWidth: true))
            .disabled(sources.isEmpty)
        }
    }
}
