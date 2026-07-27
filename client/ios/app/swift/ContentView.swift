// =============================================================================
// ContentView.swift — vỏ app + điều hướng giữa ba màn theo trạng thái model.
//
//   connect → sourcePicker → stream        (chỉ có vai CLIENT)
//
// GIAO DIỆN TRẦN (2026-07-27)
//   Không rail, không quầng sáng, không nút sáng/tối hay EN/VI: app chỉ còn một giao
//   diện (tối) và một ngôn ngữ. `.preferredColorScheme(.dark)` ép tối cho cả app —
//   vùng letterbox quanh khung hình phải là màu KHÔNG CÓ, chứ không phải một màu nhạt.
// =============================================================================
import SwiftUI

struct ContentView: View {
    @State private var model = SessionModel()

    var body: some View {
        Group {
            switch model.screen {
            case .connect:
                ConnectView(model: model)
            case let .sourcePicker(sources):
                SourcePickerView(model: model, sources: sources)
            case .stream:
                StreamView(model: model)
            }
        }
        .preferredColorScheme(.dark)
    }
}
