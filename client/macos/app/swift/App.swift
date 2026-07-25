// =============================================================================
// App.swift — entry point của app macOS.
//
// MỘT CỬA SỔ, HAI VAI
//   Khác iOS (client-only), app này chứa cả vai xem lẫn vai chia sẻ — kiểu AnyDesk,
//   đúng như client.exe bên Windows (docs/01 §1). Một cửa sổ duy nhất, điều hướng
//   bằng AppModel.route.
//
// KÍCH THƯỚC TỐI THIỂU
//   defaultSize đủ rộng để danh sách nguồn không bị cắt chữ, và minWidth chặn người
//   dùng bóp cửa sổ nhỏ tới mức thanh phím tắt của StreamView phải cuộn.
// =============================================================================
import SwiftUI

@main
struct DeskhubApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
                .frame(minWidth: 720, minHeight: 480)
        }
        .defaultSize(width: 1100, height: 720)
        .windowResizability(.contentMinSize)
    }
}
