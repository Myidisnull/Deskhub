// =============================================================================
// ConnectView.swift — ô nhập địa chỉ + nút Connect (vai CLIENT).
//                     Đối ứng ConnectView.swift bên iOS và MainMenuWindow bên Windows.
//
// Địa chỉ được nhớ lại cho lần sau (@AppStorage phía SessionModel). Cổng mặc định
// 47777 do tầng C++ điền khi chuỗi không có ":port" (ParseNetAddr) — Swift không lặp
// lại hằng số đó để hai nơi không lệch nhau.
// =============================================================================
import SwiftUI

struct ConnectView: View {
    @Binding var route: Route
    @Bindable var model: SessionModel

    var body: some View {
        VStack(spacing: 24) {
            Spacer()

            Image(systemName: "arrow.up.right.video")
                .font(.system(size: 44))
                .foregroundStyle(.secondary)

            Text("Connect to a computer")
                .font(.title2.bold())

            VStack(spacing: 12) {
                TextField("Host IP address (e.g. 192.168.1.5)", text: $model.address)
                    .textFieldStyle(.roundedBorder)
                    .disableAutocorrection(true)
                    .onSubmit { connect() }

                if !model.connectError.isEmpty {
                    Text(model.connectError)
                        .font(.caption)
                        .foregroundStyle(.red)
                }

                Button(action: connect) {
                    if model.isConnecting {
                        ProgressView()
                            .controlSize(.small)
                            .frame(maxWidth: .infinity)
                    } else {
                        Text("Connect")
                            .frame(maxWidth: .infinity)
                    }
                }
                .buttonStyle(.borderedProminent)
                .disabled(model.address.isEmpty || model.isConnecting)
            }
            .frame(width: 380)

            Text("The other machine must be sharing (Share this Mac, or client.exe on Windows).")
                .font(.caption)
                .foregroundStyle(.secondary)

            Spacer()

            Button("Back") { route = .home }
                .buttonStyle(.link)
        }
        .padding(40)
    }

    // Danh sách rỗng gộp hai trường hợp — host im lặng (bản cũ / mất gói) và host
    // không chia sẻ gì — thành một: cứ vào NGUỒN 0 và để ClientSession báo lỗi thật.
    // Một nguồn thì bỏ qua luôn màn chọn nguồn.
    private func connect() {
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
