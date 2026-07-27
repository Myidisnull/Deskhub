// =============================================================================
// SourcePickerView.swift — hộp "What do you want to view?" (vai CLIENT), chép theo
//                          SourcePickerDialog.cpp bên Windows: listbox CHỌN NHIỀU
//                          các nguồn "tên (WxH)", dòng gợi ý, nút View / Cancel.
//
// MÀN NÀY XEN GIỮA CONNECT VÀ CÁC CỬA SỔ XEM
//   Host chia sẻ TẤT CẢ màn hình, mỗi cái một sourceId. Chỉ hiện khi host chia sẻ
//   >1 nguồn — một nguồn thì Connect đi thẳng vào xem, giống Windows.
//
// CHỌN NHIỀU, MỖI NGUỒN MỘT CỬA SỔ — đúng như listbox LBS_MULTIPLESEL của Windows:
//   bấm View là mỗi nguồn đã chọn mở một ViewerWindow riêng, cửa sổ chính ẩn đi.
//   Windows chọn sẵn dòng đầu (LB_SETSEL index 0) — ở đây cũng vậy.
// =============================================================================
import SwiftUI

struct SourcePickerView: View {
    @Binding var route: Route
    @Bindable var model: SessionModel
    let sources: [Source]

    @State private var picked: Set<UInt8> = []
    @Environment(\.openWindow) private var openWindow
    @Environment(\.dismissWindow) private var dismissWindow

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            List {
                ForEach(sources) { source in
                    HStack {
                        Text(rowLabel(source))
                        Spacer()
                    }
                    .contentShape(Rectangle())
                    .listRowBackground(picked.contains(source.id)
                        ? Color.accentColor.opacity(0.25) : Color.clear)
                    .onTapGesture { toggle(source.id) }
                    .simultaneousGesture(TapGesture(count: 2).onEnded {
                        picked.insert(source.id)
                        view()
                    })
                }
            }
            .listStyle(.bordered)
            .frame(maxWidth: .infinity, maxHeight: .infinity)

            // Cùng dòng gợi ý của hộp thoại Windows.
            Text("Each one you pick opens its own window.")

            HStack {
                Spacer()
                Button("View") { view() }
                    .buttonStyle(.borderedProminent)
                    .disabled(picked.isEmpty)
                Button("Cancel") { route = .menu }
            }
        }
        .padding(12)
        .onAppear {
            // Chọn sẵn dòng đầu, như LB_SETSEL của Windows.
            if picked.isEmpty, let first = sources.first { picked = [first.id] }
        }
    }

    // Cùng chuỗi dòng mà SourcePickerDialog bên Windows dựng: "tên (WxH)".
    private func rowLabel(_ source: Source) -> String {
        let name = source.name.isEmpty ? "Source \(source.id)" : source.name
        return "\(name) (\(source.width)x\(source.height))"
    }

    private func toggle(_ id: UInt8) {
        if picked.contains(id) { picked.remove(id) } else { picked.insert(id) }
    }

    private func view() {
        let chosen = sources.filter { picked.contains($0.id) }
        guard !chosen.isEmpty else { return }
        route = .menu // cửa sổ chính quay về menu trước khi ẩn — lúc hiện lại là menu
        openViewers(chosen, address: model.address,
                    openWindow: openWindow, dismissWindow: dismissWindow)
    }
}
