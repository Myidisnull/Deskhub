// =============================================================================
// SourcePickerView.swift — chọn cửa sổ muốn xem (vai CLIENT).
//                          Đối ứng SourcePickerView bên iOS và
//                          client/windows/ui/SourcePickerDialog.
//
// Chỉ hiện khi host chia sẻ NHIỀU HƠN một nguồn — một nguồn thì ConnectView vào
// thẳng màn xem, khỏi bắt người dùng bấm một cái không có lựa chọn nào.
// =============================================================================
import SwiftUI

struct SourcePickerView: View {
    @Binding var route: Route
    @Bindable var model: SessionModel
    let sources: [Source]

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text("Select a source to view")
                    .font(.headline)
                Spacer()
                Button("Back") { route = .connect }
            }
            .padding()

            Divider()

            List(sources) { source in
                Button {
                    model.startStream(sourceId: source.id)
                    route = .stream
                } label: {
                    HStack {
                        Image(systemName: "macwindow")
                            .foregroundStyle(.secondary)
                        VStack(alignment: .leading, spacing: 2) {
                            Text(source.name)
                            Text("\(source.width)×\(source.height)")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                        Spacer()
                        Image(systemName: "chevron.right")
                            .foregroundStyle(.secondary)
                    }
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
            }
        }
    }
}
