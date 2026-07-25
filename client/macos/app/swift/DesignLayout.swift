// =============================================================================
// DesignLayout.swift — khung của một màn hình: cột nội dung gutter 56 + thanh hành
// động ghim dưới cùng, và hai dạng lưới đều mà bản thiết kế dùng.
//
// LIÊN QUAN: các *View.swift (mọi màn desktop đều bắt đầu bằng ScreenBody)
// =============================================================================
import AppKit
import SwiftUI

// MARK: - Bố cục màn hình

// Thân màn: gutter 56, cột nội dung, thanh hành động dưới cùng tuỳ chọn.
struct ScreenBody<Content: View, Bar: View>: View {
    var gutter: CGFloat = DS.screenGutter
    var topPad: CGFloat = 34
    var spacing: CGFloat = 26
    var scrolls = true
    // Cờ tường minh thay vì so `Bar.self == EmptyView.self`: so kiểu metatype ở đây
    // vừa khó đọc vừa im lặng sai nếu ai đó truyền vào một view rỗng kiểu khác.
    fileprivate var hasBar = true
    @ViewBuilder var content: Content
    @ViewBuilder var bar: Bar

    init(
        gutter: CGFloat = DS.screenGutter,
        topPad: CGFloat = 34,
        spacing: CGFloat = 26,
        scrolls: Bool = true,
        @ViewBuilder content: () -> Content,
        @ViewBuilder bar: () -> Bar
    ) {
        self.gutter = gutter
        self.topPad = topPad
        self.spacing = spacing
        self.scrolls = scrolls
        hasBar = true
        self.content = content()
        self.bar = bar()
    }

    var body: some View {
        VStack(spacing: 0) {
            Group {
                if scrolls {
                    ScrollView { column }
                } else {
                    column
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)

            if hasBar {
                Rectangle()
                    .fill(DS.borderHairline)
                    .frame(height: DS.hairline)
                HStack(spacing: 14) { bar }
                    .padding(.horizontal, gutter)
                    .padding(.vertical, 18)
                    .background(DS.glass)
            }
        }
    }

    private var column: some View {
        VStack(alignment: .leading, spacing: spacing) {
            content
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal, gutter)
        .padding(.top, topPad)
        .padding(.bottom, 26)
    }
}

extension ScreenBody where Bar == EmptyView {
    init(
        gutter: CGFloat = DS.screenGutter,
        topPad: CGFloat = 34,
        spacing: CGFloat = 26,
        scrolls: Bool = true,
        @ViewBuilder content: () -> Content
    ) {
        self.init(gutter: gutter, topPad: topPad, spacing: spacing, scrolls: scrolls,
                  content: content, bar: { EmptyView() })
        hasBar = false
    }
}

// MARK: - Lưới

// Lưới hai/ba cột đều nhau — bản thiết kế dùng đúng hai dạng lưới và không dùng dạng
// nào khác, nên gói lại ở đây thay vì lặp GridItem ở từng màn.
struct EvenGrid<Content: View>: View {
    var columns: Int = 2
    var spacing: CGFloat = 18
    @ViewBuilder var content: Content

    var body: some View {
        LazyVGrid(
            columns: Array(repeating: GridItem(.flexible(), spacing: spacing, alignment: .top), count: columns),
            alignment: .leading,
            spacing: spacing
        ) {
            content
        }
    }
}
