import SwiftUI

struct MainMenuSidebar: View {
    @Binding var page: DeskhubPage

    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Text("Deskhub")
                .font(.system(size: 26, weight: .bold))
                .foregroundStyle(.white)
                .padding(16)

            ForEach(DeskhubPage.allCases) { item in
                SidebarItem(item: item, selected: page == item) { page = item }
            }

            Spacer(minLength: 0)

            if let url = URL(string: DeskhubClient.string(DHStrProjectUrl)) {
                Link(DeskhubClient.string(DHStrProjectLinkLabel), destination: url)
                    .foregroundStyle(DeskhubPalette.navText)
                    .padding(.horizontal, 16)
            }

            Text(DeskhubClient.buffered(64) { dh_version_line($0, $1) })
                .font(.caption)
                .foregroundStyle(DeskhubPalette.footnote)
                .padding(16)
        }
        .frame(width: 180)
        .frame(maxHeight: .infinity)
        .background(DeskhubPalette.sidebar)
    }
}

private struct SidebarItem: View {
    let item: DeskhubPage
    let selected: Bool
    let onClick: () -> Void

    @State private var hovering = false

    var body: some View {
        Button(action: onClick) {
            Text(item.label)
                .font(.system(size: 14, weight: selected ? .bold : .regular))
                .foregroundStyle(selected ? Color.white : DeskhubPalette.navText)
                .frame(maxWidth: .infinity, minHeight: 42, alignment: .leading)
                .padding(.horizontal, 16)
                .background(
                    RoundedRectangle(cornerRadius: 8).fill(fill)
                )
                .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .onHover { hovering = $0 }
        .padding(.horizontal, 10)
        .padding(.bottom, 10)
    }

    private var fill: Color {
        if selected { return DeskhubPalette.accent }
        return hovering ? DeskhubPalette.sidebarHover : Color.clear
    }
}
