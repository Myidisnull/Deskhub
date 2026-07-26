// =============================================================================
// DesignLayout.swift — khung của một màn hình: thanh trên cùng (danh tính + hai
// tuỳ chọn toàn app), cột nội dung, và thanh hành động ghim dưới cùng.
// Đối ứng client/macos/app/swift/DesignLayout.swift.
//
// THANH TRÊN CÙNG THAY CHO RAIL DỌC
//   Bản desktop có một rail dọc rộng 74pt bên trái: ba đích + sáng/tối + ngôn ngữ.
//   Trên điện thoại thì rail đó ăn 19% bề ngang màn hình để phục vụ một menu chỉ có
//   MỘT đích thật (mobile là client-only — không có vai host, không có màn chia sẻ).
//   Nên rail biến mất; hai tuỳ chọn ở chân rail chuyển lên góc phải thanh trên cùng,
//   và việc điều hướng do chính luồng màn hình lo.
//
// THANH ĐÁY LÀ CHỖ ĐẶT HÀNH ĐỘNG CHÍNH, KHÔNG PHẢI CHỖ CHỨA ĐỒ THỪA
//   Trên desktop nó là một dải phụ dưới chân cửa sổ. Trên điện thoại cầm một tay, đó
//   là vùng DUY NHẤT ngón cái với tới thoải mái — nên nút chính của mỗi màn nằm ở
//   đây, và nó cao 52 (controlHeightLg) chứ không phải 44.
//
// LIÊN QUAN: ConnectView.swift, SourcePickerView.swift (mọi màn không phải màn xem)
// =============================================================================
import SwiftUI

// MARK: - Bố cục màn hình

struct ScreenBody<Content: View, Bar: View>: View {
    var gutter: CGFloat = DS.screenGutter
    var topPad: CGFloat = 12
    var spacing: CGFloat = 22
    // Cờ tường minh thay vì so `Bar.self == EmptyView.self`: so kiểu metatype ở đây
    // vừa khó đọc vừa im lặng sai nếu ai đó truyền vào một view rỗng kiểu khác.
    fileprivate var hasBar = true
    @ViewBuilder var content: Content
    @ViewBuilder var bar: Bar

    init(
        gutter: CGFloat = DS.screenGutter,
        topPad: CGFloat = 12,
        spacing: CGFloat = 22,
        @ViewBuilder content: () -> Content,
        @ViewBuilder bar: () -> Bar
    ) {
        self.gutter = gutter
        self.topPad = topPad
        self.spacing = spacing
        hasBar = true
        self.content = content()
        self.bar = bar()
    }

    var body: some View {
        VStack(spacing: 0) {
            ScrollView {
                VStack(alignment: .leading, spacing: spacing) {
                    content
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(.horizontal, gutter)
                .padding(.top, topPad)
                .padding(.bottom, 24)
            }
            // Bàn phím ảo đẩy thanh đáy lên trên nó (KHÔNG ignoresSafeArea(.keyboard)):
            // nút chính bị bàn phím che là nút không tồn tại, và người dùng vừa gõ
            // xong địa chỉ thì đúng lúc cần bấm nó nhất.
            .scrollDismissesKeyboard(.interactively)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)

            if hasBar {
                Rectangle()
                    .fill(DS.borderHairline)
                    .frame(height: DS.hairline)
                VStack(alignment: .leading, spacing: 12) { bar }
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.horizontal, gutter)
                    .padding(.top, 12)
                    .padding(.bottom, 12)
                    .background(DS.glass)
            }
        }
    }
}

extension ScreenBody where Bar == EmptyView {
    init(
        gutter: CGFloat = DS.screenGutter,
        topPad: CGFloat = 12,
        spacing: CGFloat = 22,
        @ViewBuilder content: () -> Content
    ) {
        self.init(gutter: gutter, topPad: topPad, spacing: spacing, content: content, bar: { EmptyView() })
        hasBar = false
    }
}

// MARK: - Thanh trên cùng

// Danh tính app bên trái, sáng/tối + ngôn ngữ bên phải — đúng bộ đôi mà bản desktop
// đặt ở chân rail. `leading` cho màn con đặt nút quay lại vào chỗ của biểu tượng.
struct TopBar<Leading: View>: View {
    @ViewBuilder var leading: Leading

    @State private var app = AppState.shared

    var body: some View {
        HStack(spacing: 10) {
            leading
            Spacer(minLength: 8)

            Button { app.toggleTheme() } label: {
                Image(systemName: app.isDark ? "sun.max" : "moon")
                    .font(.system(size: 17))
            }
            .buttonStyle(DSIconButtonStyle(side: 38, radius: DS.radiusSm))
            .accessibilityLabel(app.isDark ? tr("appearanceLight") : tr("appearanceDark"))

            Button { app.toggleLang() } label: {
                Text(app.lang.uppercased())
                    .font(DS.mono(DS.textBodySm, weight: .medium))
            }
            .buttonStyle(DSIconButtonStyle(side: 38, radius: DS.radiusSm, active: true))
            .accessibilityLabel(tr("language"))
        }
        .padding(.horizontal, DS.screenGutter)
        .padding(.vertical, 8)
    }
}

extension TopBar where Leading == AppMark {
    init() {
        self.init { AppMark() }
    }
}

// Danh tính: chấm mint + tên sản phẩm. Thay cho biểu tượng app ở đầu rail desktop —
// icon thật của app không lấy được ra ở iOS mà không nhét thêm một bản sao vào asset
// catalog, và một dòng chữ ở đây nói được nhiều hơn một hình vuông 34pt.
struct AppMark: View {
    var body: some View {
        HStack(spacing: 8) {
            Circle()
                .fill(DS.accent)
                .frame(width: 8, height: 8)
            Text("Deskhub")
                .font(DS.display(DS.textBodyLg, weight: .semibold))
                .tracking(DS.trackTitle(DS.textBodyLg))
                .foregroundStyle(DS.textPrimary)
        }
    }
}
