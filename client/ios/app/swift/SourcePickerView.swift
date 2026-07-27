// =============================================================================
// SourcePickerView.swift — màn "Chọn thứ muốn xem" (vai CLIENT).
//
// MÀN NÀY XEN GIỮA CONNECT VÀ VIEWER
//   Host chia sẻ TẤT CẢ màn hình, mỗi cái một sourceId. Không có màn này thì client
//   luôn xem nguồn 0 và không có đường nào tới các nguồn còn lại.
//
// MỘT LẦN MỘT NGUỒN
//   dh_start nhận MỘT sourceId, nên chọn kiểu radio. Đổi sang nguồn khác giữa phiên
//   thì dùng nút Display ở màn xem (SessionModel.switchSource) — không phải quay về
//   đây.
//
// HOST IM LẶNG KHÔNG PHẢI LÀ LỖI
//   Host đời trước GĐ6 không biết LIST_SOURCES. Lúc đó màn này KHÔNG hiện ra — model
//   đi thẳng sang màn xem với nguồn 0, đúng hành vi cũ. Người dùng không được thấy
//   một màn trống và một lời báo lỗi cho chuyện họ không làm gì sai.
//
// GIAO DIỆN TRẦN (2026-07-27): SwiftUI dựng sẵn, không hệ thiết kế riêng.
// =============================================================================
import SwiftUI

struct SourcePickerView: View {
    @Bindable var model: SessionModel
    let sources: [Source]

    @State private var picked: UInt8?

    private var selectedId: UInt8 { picked ?? sources.first?.id ?? 0 }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(model.address)
                .font(.headline)

            ForEach(sources) { source in
                Button {
                    picked = source.id
                } label: {
                    HStack(spacing: 12) {
                        Image(systemName: source.id == selectedId
                            ? "largecircle.fill.circle"
                            : "circle")
                        VStack(alignment: .leading) {
                            // Host cắt tên ở 64 byte; tên rỗng thì hiện "Source N".
                            Text(source.name.isEmpty ? "Source \(source.id)" : source.name)
                            Text("\(source.width)×\(source.height)")
                                .font(.caption)
                                .foregroundStyle(.secondary)
                        }
                        Spacer()
                    }
                    .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
            }

            Spacer()

            Button("Start viewing") { model.startStream(sourceId: selectedId) }
                .buttonStyle(.borderedProminent)
                .frame(maxWidth: .infinity)
                .disabled(sources.isEmpty)
        }
        .padding()
    }
}
