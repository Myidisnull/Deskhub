// =============================================================================
// ContentView.swift — vỏ app + điều hướng.
//
// GIAO DIỆN TRẦN (2026-07-27)
//   Không rail, không quầng sáng, không hệ thiết kế riêng, không màn Home. App có
//   đúng hai chế độ và một ô chọn phân đoạn dựng sẵn để đổi giữa chúng:
//
//     Connect — kết nối máy khác (client): connect → sourcePicker → stream
//     Share   — chia sẻ máy này (host)
//
//   Màn xem chiếm TRỌN cửa sổ, không có ô chọn nào ở trên.
//
//   HomeView cũ chỉ là một trang khởi hành gồm hai ô lớn và danh sách máy gần đây;
//   danh sách đó đã bỏ, nên trang này không còn gì để nói và đã xoá luôn.
//
// `.preferredColorScheme(.dark)`: một giao diện duy nhất — nền sáng bỏ 2026-07-27.
// =============================================================================
import SwiftUI

enum Route {
    case connect
    case sourcePicker([Source])
    case share
    case stream
}

struct ContentView: View {
    @State private var route: Route = .connect
    @State private var session = SessionModel()
    @State private var agent = AgentModel()

    var body: some View {
        Group {
            if case .stream = route {
                StreamView(route: $route, model: session)
            } else {
                VStack(spacing: 0) {
                    Picker("", selection: modeBinding) {
                        Text("Connect").tag(0)
                        Text("Share this Mac").tag(1)
                    }
                    .pickerStyle(.segmented)
                    .labelsHidden()
                    .padding(12)

                    Divider()

                    screen
                        .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
            }
        }
        .preferredColorScheme(.dark)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    // Màn chọn nguồn thuộc chế độ Connect, nên nó cũng cho ô chọn về vị trí 0.
    private var modeBinding: Binding<Int> {
        Binding(
            get: {
                if case .share = route { return 1 }
                return 0
            },
            set: { newValue in
                if newValue == 1 {
                    agent.refreshPermissions()
                    route = .share
                } else {
                    route = .connect
                }
            }
        )
    }

    @ViewBuilder private var screen: some View {
        switch route {
        case .connect:
            ConnectView(route: $route, model: session)
        case let .sourcePicker(sources):
            SourcePickerView(route: $route, model: session, sources: sources)
        case .share:
            ShareView(route: $route, model: agent)
        case .stream:
            // Không tới được: nhánh .stream đã bị chặn ở trên. `case` này chỉ để
            // `switch` phủ hết enum mà không cần `default`.
            EmptyView()
        }
    }
}
