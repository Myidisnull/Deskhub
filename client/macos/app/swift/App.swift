// =============================================================================
// App.swift — entry point của app macOS.
//
// HỆ CỬA SỔ — chép theo app Win32 (2026-07-27):
//   • "main"   — cửa sổ chính (MainMenuWindow): menu / hộp chọn nguồn / màn phiên
//                chia sẻ, kích thước ôm sát nội dung từng màn.
//   • "viewer" — MỖI NGUỒN XEM MỘT CỬA SỔ (ViewerFrame trong Viewer.cpp), mở song
//                song qua openWindow(value: ViewerRequest).
//   Windows ẩn menu suốt lúc xem và hiện lại khi cửa sổ xem cuối cùng đóng
//   (g_openFrames); ở đây ViewerRegistry đếm cửa sổ xem để làm đúng như thế.
// =============================================================================
import Observation
import SwiftUI

// Đếm cửa sổ xem đang mở — đối ứng g_openFrames của RunViewer. Về 0 là mở lại
// cửa sổ chính (ViewerWindow.onDisappear làm việc đó).
@MainActor @Observable
final class ViewerRegistry {
    var count = 0
}

@main
struct DeskhubApp: App {
    @State private var viewers = ViewerRegistry()

    var body: some Scene {
        Window("Deskhub", id: "main") {
            ContentView()
                .environment(viewers)
        }
        .windowResizability(.contentSize)

        WindowGroup(id: "viewer", for: ViewerRequest.self) { $request in
            if let request {
                ViewerWindow(request: request)
                    .environment(viewers)
            }
        }
        .windowResizability(.contentMinSize)
        // Cỡ ban đầu của cửa sổ xem bên Windows: 1024×600 + thanh trên 34px.
        .defaultSize(width: 1024, height: 634)
    }
}
