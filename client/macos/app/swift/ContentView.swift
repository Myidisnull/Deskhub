// =============================================================================
// ContentView.swift — nội dung CỬA SỔ CHÍNH + điều hướng.
//
// GIỐNG BẢN WINDOWS (2026-07-27)
//   Bố cục chép theo app Win32: MỘT màn chính chứa cả hai hộp Host mode /
//   Client mode (MainMenuWindow.cpp), không có ô chọn chế độ. Từ đó rẽ:
//
//     Share...  → .sharing (đối ứng SessionWindow "Deskhub - sharing")
//     Connect   → .sourcePicker (nếu host chia sẻ >1 nguồn) → mở CỬA SỔ XEM RIÊNG
//                 cho từng nguồn (ViewerWindow, xem StreamView.swift) và ẩn cửa sổ
//                 chính — giống MainMenuWindow ẩn đi khi RunViewer chạy.
//
// Không ép dark mode: Win32 vẽ theo màu hệ thống, ở đây cũng theo hệ thống.
// =============================================================================
import SwiftUI

enum Route {
    case menu
    case sourcePicker([Source])
    case sharing
}

struct ContentView: View {
    @State private var route: Route = .menu
    @State private var session = SessionModel()
    @State private var agent = AgentModel()

    var body: some View {
        // Mỗi màn tự khai khung của nó (cùng cỡ với cửa sổ Win32 tương ứng);
        // window resizability `.contentSize` làm cửa sổ ôm đúng khung này.
        Group {
            switch route {
            case .menu:
                MainMenuView(route: $route, session: session, agent: agent)
                    .frame(width: 500)
            case let .sourcePicker(sources):
                SourcePickerView(route: $route, model: session, sources: sources)
                    .frame(width: 460, height: 340)
            case .sharing:
                SharingSessionView(route: $route, model: agent)
                    .frame(width: 460, height: 330)
            }
        }
        .navigationTitle(windowTitle)
    }

    // Tiêu đề cửa sổ đổi theo màn, cùng chuỗi với các cửa sổ của bản Windows.
    private var windowTitle: String {
        switch route {
        case .menu: "Deskhub - stream & remotely control an application"
        case .sourcePicker: "What do you want to view?"
        case .sharing: "Deskhub - sharing"
        }
    }
}
