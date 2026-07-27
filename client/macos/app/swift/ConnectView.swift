// =============================================================================
// ConnectView.swift — ô nhập IP + nút Connect (vai CLIENT).
//
// GIAO DIỆN TRẦN (2026-07-27)
//   SwiftUI dựng sẵn, không hệ thiết kế riêng, không chữ hướng dẫn, không danh sách
//   máy gần đây. Màn này còn đúng hai thứ: một ô nhập và một nút.
//
//   Người dùng chỉ gõ IP: cổng luôn là 47777 và do tầng C++ điền (ParseNetAddr từ
//   chối chuỗi có ':') — Swift không lặp lại hằng số đó để hai nơi không lệch nhau.
// =============================================================================
import SwiftUI

struct ConnectView: View {
    @Binding var route: Route
    @Bindable var model: SessionModel

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            TextField("Host IP address", text: $model.address)
                .textFieldStyle(.roundedBorder)
                .onSubmit(connect)
                .disabled(model.isConnecting)
                .frame(maxWidth: 360)

            HStack(spacing: 12) {
                Button("Connect", action: connect)
                    .buttonStyle(.borderedProminent)
                    .disabled(model.address.isEmpty || model.isConnecting)

                if model.isConnecting {
                    ProgressView().controlSize(.small)
                    Text("Asking the host what it is sharing…")
                        .foregroundStyle(.secondary)
                }
            }

            if !model.connectError.isEmpty {
                Text(model.connectError)
                    .foregroundStyle(.red)
                    .fixedSize(horizontal: false, vertical: true)
            }

            Spacer()
        }
        .padding()
        .frame(maxWidth: .infinity, alignment: .leading)
    }

    // Danh sách rỗng gộp hai trường hợp — host im lặng (bản cũ / mất gói) và host
    // không chia sẻ gì — thành một: cứ vào NGUỒN 0 và để ClientSession báo lỗi thật.
    // Một nguồn thì bỏ qua luôn màn chọn nguồn: bắt người dùng bấm một cái không có
    // lựa chọn nào là một bước thừa.
    private func connect() {
        guard !model.address.isEmpty, !model.isConnecting else { return }
        Task {
            let sources = await model.listSources()
            if sources.count > 1 {
                route = .sourcePicker(sources)
            } else {
                model.startStream(sourceId: sources.first?.id ?? 0)
                route = .stream
            }
        }
    }
}
