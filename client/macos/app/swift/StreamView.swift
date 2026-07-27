// =============================================================================
// StreamView.swift — cửa sổ XEM + ĐIỀU KHIỂN, chép theo cửa sổ xem của bản Windows
//                    (Viewer.cpp). MỖI NGUỒN MỘT CỬA SỔ, mở song song — ViewerWindow
//                    ở đây đối ứng ViewerFrame: sở hữu một phiên (StreamModel →
//                    ClientSession) và tự dọn khi đóng.
//
// BỐ CỤC MỘT CỬA SỔ — cả cửa sổ là video (aspect-FIT, letterbox đen):
//   Dòng số liệu (fps/kbps/loss/RTT/e2e) và gợi ý F9 nằm ở HEADER — thanh tiêu đề
//   của cửa sổ (navigationSubtitle), không chiếm một hàng nội dung riêng như thanh
//   34px của Viewer.cpp. KHÔNG có nút Disconnect: đóng cửa sổ là ngắt (WM_CLOSE bên
//   Windows cũng chính là đường đó). KHÔNG có nút Fill/Fit hay Lock mouse: video
//   luôn fit, còn khoá chuột là PHÍM F9 — chữ gợi ý trên header đổi theo trạng
//   thái. Cũng KHÔNG có nút Display: xem nguồn khác = một cửa sổ khác.
//
// MENU HIỆN LẠI KHI CỬA SỔ XEM CUỐI CÙNG ĐÓNG — đối ứng g_openFrames của RunViewer:
//   Windows ẩn menu suốt lúc xem và hiện lại khi vòng message kết thúc; ở đây
//   ViewerRegistry đếm cửa sổ, về 0 thì mở lại cửa sổ chính.
//
// PHIÊN ĐỨT TỪ PHÍA NATIVE
//   Windows hiện MessageBox "Connection ended: <lý do>" rồi đóng cửa sổ — ở đây là
//   một alert, bấm OK cũng đóng cửa sổ này.
//
// NỀN ĐEN, KỂ CẢ Ở GIAO DIỆN SÁNG CỦA HỆ
//   Vùng letterbox quanh khung hình phải là màu KHÔNG CÓ, chứ không phải một màu
//   nhạt — nếu không, mắt sẽ đọc nó thành một phần của hình.
// =============================================================================
import AVFoundation
import SwiftUI

// Giá trị mở một cửa sổ xem (openWindow(value:)). Codable vì SwiftUI phục hồi cửa
// sổ qua state restoration.
struct ViewerRequest: Codable, Hashable {
    var address: String
    var sourceId: UInt8
    var name: String
}

// MARK: - Cửa sổ xem (đối ứng ViewerFrame của Viewer.cpp)

struct ViewerWindow: View {
    @State private var model: StreamModel
    @Environment(\.dismiss) private var dismiss
    @Environment(\.openWindow) private var openWindow
    @Environment(ViewerRegistry.self) private var viewers

    init(request: ViewerRequest) {
        _model = State(initialValue: StreamModel(
            address: request.address,
            sourceId: request.sourceId,
            sourceName: request.name
        ))
    }

    var body: some View {
        StreamView(model: model) { dismiss() }
            .navigationTitle(title)
            .navigationSubtitle(subtitle)
            .frame(minWidth: 640, idealWidth: 1024, maxWidth: .infinity,
                   minHeight: 400, idealHeight: 634, maxHeight: .infinity)
            .task {
                viewers.count += 1
                await model.start()
            }
            .onDisappear {
                model.setLayer(nil)
                model.disconnect()
                viewers.count -= 1
                // Cửa sổ xem cuối cùng đóng -> menu quay lại, như RunViewer trả về.
                if viewers.count <= 0 { openWindow(id: "main") }
            }
            .alert("Deskhub", isPresented: failedShown) {
                Button("OK") { dismiss() }
            } message: {
                // Cùng chuỗi MessageBox của RunViewer khi không mở được phiên nào.
                Text("Could not open a viewing session - check the address and that "
                    + "the other machine is sharing.")
            }
    }

    // Giống OpenFrame bên Windows: "Deskhub - viewing: <tên nguồn>" khi biết tên.
    private var title: String {
        model.sourceName.isEmpty
            ? "Deskhub - viewing"
            : "Deskhub - viewing: \(model.sourceName)"
    }

    // Header mang cả số liệu lẫn gợi ý F9 — nội dung cửa sổ chỉ còn video.
    private var subtitle: String {
        let stats = model.statusLine.isEmpty ? "connecting..." : model.statusLine
        let hint = model.mouseLocked
            ? "Mouse locked - press F9 to release"
            : "Press F9 to lock mouse"
        return "\(stats) · \(hint)"
    }

    private var failedShown: Binding<Bool> {
        Binding(get: { model.failedToStart }, set: { _ in })
    }
}

// MARK: - Nội dung cửa sổ

struct StreamView: View {
    @Bindable var model: StreamModel
    let onEnd: () -> Void

    var body: some View {
        RemoteView(
            model: model,
            videoSize: videoSize,
            mouseLocked: model.mouseLocked,
            onLayerReady: { layer in model.setLayer(layer) },
            onLockChanged: { model.mouseLocked = $0 }
        )
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .background(Color.black)
        .environment(\.colorScheme, .dark)
        .alert("Deskhub", isPresented: endedAlertShown) {
            Button("OK") { onEnd() }
        } message: {
            Text("Connection ended: \(model.endReason)")
        }
    }

    private var videoSize: CGSize {
        CGSize(width: Double(model.videoWidth), height: Double(model.videoHeight))
    }

    private var endedAlertShown: Binding<Bool> {
        Binding(get: { !model.endReason.isEmpty && !model.failedToStart }, set: { _ in })
    }
}
