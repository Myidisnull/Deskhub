// =============================================================================
// ContentView.swift — vỏ cửa sổ + điều hướng. Dựng theo `WindowShell` + `Rail` trong
// parts.jsx của dự án thiết kế.
//
// HAI NHÁNH, MỘT CỬA SỔ
//   home → connect → sourcePicker → stream   (vai CLIENT: xem máy khác)
//   home → share                             (vai HOST: chia sẻ máy này)
//
// Hai model sống ở đây và tồn tại suốt vòng đời app, không phải theo từng màn: một
// phiên đang chạy phải sống sót khi người dùng đi qua lại giữa các màn hình.
//
// MỘT NGUỒN SÁNG, KHÔNG PHẢI MỘT LỚP NỀN
//   Vệt cobalt mờ phía sau là "đèn" của bản thiết kế: nó hắt từ ngoài khung vào, cho
//   nền gần-đen có chiều sâu mà không cần đổ bóng ở đâu cả. Nó nằm NGOÀI mép cửa sổ
//   (offset dương ra ngoài) đúng như bản gốc — kéo vào giữa màn hình sẽ thành một
//   quầng trang trí, khác hẳn về cảm giác.
//
// RAIL BIẾN MẤT Ở MÀN XEM
//   Bản thiết kế vẽ màn xem KHÔNG có rail — cả cửa sổ là hình từ máy kia, mọi điều
//   khiển nằm trong HUD nổi. Vỏ tự ẩn rail theo route, thay vì bắt mỗi màn phải nhớ.
//
// MÀN CHIA SẺ GỘP LÀM MỘT (khác bản macOS cũ có thêm màn "session")
//   Bản thiết kế vẽ chọn nguồn và tình trạng phiên trên CÙNG một màn, và đó là quyết
//   định đúng: trong lúc đang chia sẻ, việc người ta hay làm nhất là THÊM hoặc BỎ một
//   nguồn. Tách hai màn thì thao tác đó thành "quay lại → chọn lại → bắt đầu lại", mà
//   bắt đầu lại sẽ ngắt phiên của người đang xem. Bản Windows đã gộp vì đúng lý do
//   này (xem SharePage.xaml).
// =============================================================================
import SwiftUI

enum Route: Equatable {
    case home
    case connect
    case sourcePicker([Source])
    case stream
    case share

    // Source không Equatable (nó đến từ C), nhưng Route thì cần — so theo id là đủ
    // để SwiftUI biết màn hình có đổi hay không.
    static func == (lhs: Route, rhs: Route) -> Bool {
        switch (lhs, rhs) {
        case (.home, .home), (.connect, .connect), (.stream, .stream), (.share, .share):
            true
        case let (.sourcePicker(lhsSources), .sourcePicker(rhsSources)):
            lhsSources.map(\.id) == rhsSources.map(\.id)
        default:
            false
        }
    }

    // Mục nào của rail đang sáng. Màn chọn nguồn vẫn thuộc nhánh "kết nối" — nó là một
    // bước giữa đường chứ không phải một đích riêng.
    var railItem: RailItem? {
        switch self {
        case .home: .home
        case .connect, .sourcePicker: .connect
        case .share: .share
        case .stream: nil
        }
    }
}

enum RailItem { case home, connect, share }

struct ContentView: View {
    @State private var route: Route = .home
    @State private var session = SessionModel()
    @State private var agent = AgentModel()
    @State private var app = AppState.shared

    var body: some View {
        ZStack {
            DS.bgWindow.ignoresSafeArea()
            Glow(size: 900)
                .ignoresSafeArea()

            HStack(spacing: 0) {
                // Màn xem chiếm trọn cửa sổ: không rail, không gì ngoài hình.
                if route != .stream {
                    SideRail(active: route.railItem) { item in
                        switch item {
                        case .home: route = .home
                        case .connect: route = .connect
                        case .share:
                            agent.refreshPermissions()
                            route = .share
                        }
                    }
                }

                screen
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .foregroundStyle(DS.textPrimary)
        .preferredColorScheme(app.colorScheme)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    @ViewBuilder private var screen: some View {
        switch route {
        case .home:
            HomeView(route: $route, agent: agent, session: session)
        case .connect:
            ConnectView(route: $route, model: session)
        case let .sourcePicker(sources):
            SourcePickerView(route: $route, model: session, sources: sources)
        case .stream:
            StreamView(route: $route, model: session)
        case .share:
            ShareView(route: $route, model: agent)
        }
    }
}

// MARK: - Thanh rail

// Đúng ba đích, khớp RAIL_ITEMS của bản thiết kế. Chân rail là sáng/tối + ngôn ngữ,
// đúng chỗ bản thiết kế đặt chúng.
struct SideRail: View {
    let active: RailItem?
    let onSelect: (RailItem) -> Void

    @State private var app = AppState.shared

    var body: some View {
        VStack(spacing: 0) {
            Image(nsImage: NSApp.applicationIconImage)
                .resizable()
                .frame(width: 34, height: 34)
                .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
                .padding(.top, 18)
                .padding(.bottom, 10)

            VStack(spacing: 10) {
                railButton(.home, icon: "desktopcomputer.and.macbook", help: tr("machines"))
                railButton(.connect, icon: "video", help: tr("connect"))
                railButton(.share, icon: "square.on.square", help: tr("shareThisMac"))
            }
            .padding(.top, 8)

            Spacer(minLength: 0)

            VStack(spacing: 8) {
                Button {
                    app.toggleTheme()
                } label: {
                    Image(systemName: app.isDark ? "sun.max" : "moon")
                        .font(.system(size: 18))
                }
                .buttonStyle(DSIconButtonStyle())
                .help(app.isDark ? tr("appearanceLight") : tr("appearanceDark"))

                Button { app.toggleLang() } label: {
                    Chip(text: app.lang.uppercased(), active: true)
                }
                .buttonStyle(.plain)
            }
            .padding(.bottom, 18)
        }
        .frame(width: DS.railWidth)
        .background(DS.surfaceCard)
        .overlay(alignment: .trailing) {
            Rectangle()
                .fill(DS.borderHairline)
                .frame(width: DS.hairline)
        }
    }

    private func railButton(_ item: RailItem, icon: String, help: String) -> some View {
        Button { onSelect(item) } label: {
            Image(systemName: icon)
                .font(.system(size: 18, weight: .regular))
        }
        .buttonStyle(DSIconButtonStyle(active: active == item))
        .help(help)
    }
}
