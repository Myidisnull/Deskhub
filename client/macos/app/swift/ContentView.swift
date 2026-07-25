// =============================================================================
// ContentView.swift — root view, điều hướng giữa năm màn theo AppModel.route.
//
// HAI NHÁNH, MỘT CỬA SỔ
//   home → connect → sourcePicker → stream   (vai CLIENT: xem máy khác)
//   home → share   → session                 (vai HOST: chia sẻ máy này)
//
// Hai model sống ở đây và tồn tại suốt vòng đời app, không phải theo từng màn: một
// phiên đang chạy phải sống sót khi người dùng đi qua lại giữa các màn hình.
// =============================================================================
import SwiftUI

enum Route: Equatable {
    case home
    case connect
    case sourcePicker([Source])
    case stream
    case share
    case session

    // Source không Equatable (nó đến từ C), nhưng Route thì cần — so theo id là đủ
    // để SwiftUI biết màn hình có đổi hay không.
    static func == (lhs: Route, rhs: Route) -> Bool {
        switch (lhs, rhs) {
        case (.home, .home), (.connect, .connect), (.stream, .stream),
             (.share, .share), (.session, .session):
            true
        case let (.sourcePicker(lhsSources), .sourcePicker(rhsSources)):
            lhsSources.map(\.id) == rhsSources.map(\.id)
        default:
            false
        }
    }
}

struct ContentView: View {
    @State private var route: Route = .home
    @State private var session = SessionModel()
    @State private var agent = AgentModel()

    var body: some View {
        Group {
            switch route {
            case .home:
                HomeView(route: $route, model: agent)
            case .connect:
                ConnectView(route: $route, model: session)
            case let .sourcePicker(sources):
                SourcePickerView(route: $route, model: session, sources: sources)
            case .stream:
                StreamView(route: $route, model: session)
            case .share:
                ShareView(route: $route, model: agent)
            case .session:
                SessionView(route: $route, model: agent)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}
