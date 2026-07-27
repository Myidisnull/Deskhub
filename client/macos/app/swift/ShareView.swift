// =============================================================================
// ShareView.swift — màn phiên chia sẻ, chép theo cửa sổ "Deskhub - sharing"
//                   (SessionWindow.cpp bên Windows): một danh sách nguồn đang chia
//                   sẻ, một dòng nhắc, một nút Stop sharing. Không còn gì khác.
//
// DÒNG DANH SÁCH CÙNG ĐỊNH DẠNG VỚI LABEL CỦA SessionSourceRow:
//   "tên  (WxH, viewer connected)" — SetRows bên Windows dựng đúng chuỗi này.
//
// PHIÊN TỰ KẾT THÚC — vòng Recv có thể tự dừng (lỗi socket, mọi màn hình bị rút).
// AgentModel.poll thấy running() tắt sẽ hạ isSharing; màn này thấy thế thì quay về
// màn chính, giống SessionWindow bị đóng khi RunAgent trả về.
// =============================================================================
import SwiftUI

struct SharingSessionView: View {
    @Binding var route: Route
    @Bindable var model: AgentModel

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("Sources currently being shared:")

            List {
                if model.rows.isEmpty {
                    Text("(nothing is being shared)").foregroundStyle(.secondary)
                } else {
                    ForEach(model.rows) { row in
                        Text(label(for: row))
                    }
                }
            }
            .listStyle(.bordered)
            .frame(maxWidth: .infinity, maxHeight: .infinity)

            Text("Others connect by entering this machine's IP address.")

            HStack {
                Spacer()
                Button("Stop sharing") { stop() }
            }
        }
        .padding(12)
        .onChange(of: model.isSharing) { _, sharing in
            if !sharing { route = .menu }
        }
    }

    // Cùng chuỗi label mà publishRows bên Windows dựng cho listbox.
    private func label(for row: AgentSourceStatus) -> String {
        let viewer = row.viewerConnected ? ", viewer connected" : ""
        return "\(row.name)  (\(row.width)x\(row.height)\(viewer))"
    }

    // Đóng màn = kết thúc chia sẻ, giống WM_CLOSE của SessionWindow.
    private func stop() {
        model.stopSharing()
        route = .menu
    }
}
