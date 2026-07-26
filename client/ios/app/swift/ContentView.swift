// =============================================================================
// ContentView.swift — vỏ app + điều hướng giữa ba màn theo trạng thái model.
// Đối ứng client/macos/app/swift/ContentView.swift, trừ phần rail và nhánh HOST.
//
//   connect → sourcePicker → stream        (chỉ có vai CLIENT)
//
// MỘT NGUỒN SÁNG, KHÔNG PHẢI MỘT LỚP NỀN
//   Vệt cobalt mờ phía sau là "đèn" của bản thiết kế: nó hắt từ ngoài khung vào, cho
//   nền gần-đen có chiều sâu mà không cần đổ bóng ở đâu cả. Nó nằm NGOÀI mép màn hình
//   — kéo vào giữa sẽ thành một quầng trang trí, khác hẳn về cảm giác.
//
// MÀN XEM ĐỨNG NGOÀI CẢ VỎ LẪN GIAO DIỆN SÁNG
//   Nó không có nền cửa sổ, không có quầng, và luôn ở bảng màu TỐI kể cả khi người
//   dùng đang để giao diện sáng: vùng letterbox quanh khung hình phải là màu KHÔNG
//   CÓ, chứ không phải một màu nhạt — nếu không, mắt sẽ đọc nó thành một phần của
//   hình. Vì vậy nó nằm ở nhánh riêng, không lồng trong ZStack chung.
// =============================================================================
import SwiftUI

struct ContentView: View {
    @State private var model = SessionModel()
    @State private var app = AppState.shared

    var body: some View {
        Group {
            if case .stream = model.screen {
                StreamView(model: model)
            } else {
                shell
            }
        }
        .preferredColorScheme(app.colorScheme)
    }

    private var shell: some View {
        ZStack {
            DS.bgWindow.ignoresSafeArea()
            Glow().ignoresSafeArea()

            VStack(spacing: 0) {
                switch model.screen {
                case .connect:
                    TopBar()
                    ConnectView(model: model)
                case let .sourcePicker(sources):
                    TopBar {
                        Button {
                            model.screen = .connect
                        } label: {
                            Image(systemName: "chevron.left")
                                .font(.system(size: 16, weight: .semibold))
                        }
                        .buttonStyle(DSIconButtonStyle(side: 38, radius: DS.radiusSm))
                        .accessibilityLabel(tr("back"))
                        AppMark()
                    }
                    SourcePickerView(model: model, sources: sources)
                case .stream:
                    // Không tới được: nhánh .stream đã bị chặn ở trên. `case` này chỉ
                    // để `switch` phủ hết enum mà không cần `default` — thêm màn mới
                    // sau này thì trình dịch bắt phải xử lý ở đây.
                    EmptyView()
                }
            }
        }
        .foregroundStyle(DS.textPrimary)
    }
}
