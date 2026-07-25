// =============================================================================
// StreamView.swift — màn hình XEM + ĐIỀU KHIỂN. Đối ứng StreamView bên iOS và
//                    client/windows/ui/ViewerWindow.
//
// Video sống trong AVSampleBufferDisplayLayer (qua RemoteView); SwiftUI chỉ vẽ phần
// chrome: dòng số liệu, nút khoá chuột, nút Disconnect. Không frame nào đi qua Swift.
//
// KHÁC BẢN iOS: KHÔNG CÓ THANH PHÍM TẮT
//   iOS phải có hàng nút Esc/Tab/mũi tên vì bàn phím ảo không có những phím đó. macOS
//   có bàn phím thật — RemoteView bắt trọn và gửi thẳng, kể cả Esc, Tab, F-key.
//   Ngoại lệ duy nhất là F9: nó bị giữ lại làm phím thoát hiểm cho khoá chuột.
// =============================================================================
import AVFoundation
import SwiftUI

struct StreamView: View {
    @Binding var route: Route
    @Bindable var model: SessionModel
    @State private var mouseLocked = false

    var body: some View {
        VStack(spacing: 0) {
            header

            ZStack {
                Color.black
                RemoteView(
                    model: model,
                    videoSize: videoSize,
                    onLayerReady: { layer in DeskhubClient.setLayer(layer) },
                    onLockChanged: { mouseLocked = $0 }
                )

                if model.phase != .streaming, model.endReason.isEmpty {
                    connectingOverlay
                }
                if !model.endReason.isEmpty {
                    endedOverlay
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .background(Color.black)
        .onDisappear {
            DeskhubClient.setLayer(nil)
            model.disconnect()
        }
    }

    private var videoSize: CGSize {
        CGSize(width: Double(model.videoWidth), height: Double(model.videoHeight))
    }

    private var header: some View {
        HStack(spacing: 12) {
            Text(model.statusLine.isEmpty ? "Connecting…" : model.statusLine)
                .font(.caption.monospaced())
                .foregroundStyle(.white.opacity(0.8))
                .lineLimit(1)

            Spacer()

            // Nhãn nói rõ CÁCH thoát, không chỉ trạng thái: người dùng khoá chuột
            // xong không còn con trỏ để bấm lại nút này.
            Label(
                mouseLocked ? "Mouse locked — press F9 to release" : "Press F9 to lock mouse",
                systemImage: mouseLocked ? "lock.fill" : "lock.open"
            )
            .font(.caption)
            .foregroundStyle(mouseLocked ? .green : .white.opacity(0.6))

            Button("Disconnect") {
                model.disconnect()
                route = .connect
            }
            .controlSize(.small)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .background(Color(white: 0.09))
    }

    private var connectingOverlay: some View {
        VStack(spacing: 10) {
            ProgressView()
                .controlSize(.large)
            Text("Connecting to \(model.address)…")
                .font(.subheadline)
                .foregroundStyle(.white.opacity(0.8))
        }
    }

    private var endedOverlay: some View {
        VStack(spacing: 10) {
            Text("Session ended")
                .font(.headline)
            Text(model.endReason)
                .font(.subheadline)
                .foregroundStyle(.secondary)
            Button("Back") {
                model.disconnect()
                route = .connect
            }
            .buttonStyle(.borderedProminent)
        }
        .padding(24)
        .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 12))
    }
}
