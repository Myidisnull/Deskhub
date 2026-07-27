// =============================================================================
// SourcePickerView.swift — màn 03 "Chọn màn hình muốn xem" (vai CLIENT).
//                          Dựng theo `DesktopPick` trong desktop.jsx.
//
// MÀN NÀY XEN GIỮA CONNECT VÀ VIEWER
//   Host chia sẻ nhiều màn hình cùng lúc (multi-monitor), mỗi cái một sourceId.
//   Không có màn này thì client luôn xem nguồn 0 và không có đường nào tới các
//   nguồn còn lại.
//
// MỘT LẦN MỘT NGUỒN
//   Bản thiết kế ghi rõ trong chính chuỗi `pickHint`: "One at a time — picking another
//   switches the stream." Tầng C++ của macOS cũng đúng như vậy — dh_start nhận MỘT
//   sourceId. Nên ở đây ô tick hành xử như radio: chọn cái mới là bỏ cái cũ.
//
// HOST IM LẶNG KHÔNG PHẢI LÀ LỖI
//   Host đời trước GĐ6 không biết LIST_SOURCES. Lúc đó màn này KHÔNG hiện ra — ConnectView
//   đi thẳng sang màn xem với nguồn 0, đúng hành vi cũ. Người dùng không được thấy một
//   màn trống và một lời báo lỗi cho chuyện họ không làm gì sai.
// =============================================================================
import SwiftUI

struct SourcePickerView: View {
    @Binding var route: Route
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

            VStack(alignment: .leading, spacing: 14) {
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
                    EvenGrid(columns: 2, spacing: 10) {
                        ForEach(sources) { source in
                            SourceRow(
                                name: source.name,
                                detail: "\(source.width)×\(source.height)",
                                selected: source.id == selectedId,
                                state: source.id == selectedId ? tr("streaming") : tr("idle"),
                                tone: source.id == selectedId ? .live : .neutral,
                                rowToggles: true,
                                onToggle: { _ in picked = source.id }
                            )
                        }
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
            Spacer()
            Button(tr("back")) { route = .connect }
                .buttonStyle(DSButtonStyle(variant: .secondary))
            Button {
                model.startStream(sourceId: selectedId)
                route = .stream
            } label: {
                Label(tr("startViewing"), systemImage: "video")
            }
            .buttonStyle(DSButtonStyle(variant: .primary, size: .lg))
            .disabled(sources.isEmpty)
        }
    }
}
