// =============================================================================
// App.swift — entry point của app macOS.
//
// MỘT CỬA SỔ, HAI VAI
//   Khác iOS (client-only), app này chứa cả vai xem lẫn vai chia sẻ,
//   đúng như bản Windows (docs/01 §1). Một cửa sổ duy nhất, điều hướng bằng
//   ContentView.route.
//
// KÍCH THƯỚC
//   1280×840 là khung mặc định; minWidth chỉ chặn người dùng bóp cửa sổ tới mức
//   thanh dưới của màn xem bị cắt.
//
//
// KHÔNG CÓ THANH TIÊU ĐỀ RIÊNG
//   `WindowShell` của bản thiết kế vẽ một thanh 38px với ba chấm và tên cửa sổ. Trên
//   macOS đó CHÍNH LÀ thanh tiêu đề thật — vẽ lại một cái giả bên dưới cái thật là có
//   hai thanh chồng nhau. Giao diện sáng/tối của thanh này đi theo NSAppearance mà
//   ContentView đặt, nên nó vẫn đổi màu cùng phần còn lại.
// =============================================================================
import SwiftUI

@main
struct DeskhubApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
                .frame(minWidth: 1040, minHeight: 680)
        }
        .defaultSize(width: 1280, height: 840)
        .windowResizability(.contentMinSize)
    }
}
