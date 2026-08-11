import SwiftUI

func deskhubHeading(_ text: String) -> some View {
    Text(text)
        .font(.system(size: 19, weight: .bold))
        .foregroundStyle(DeskhubPalette.heading)
}

func deskhubSection(_ text: String) -> some View {
    Text(text)
        .font(.system(size: 15, weight: .bold))
        .foregroundStyle(DeskhubPalette.heading)
        .padding(.top, 8)
}

func deskhubHint(_ text: String) -> some View {
    Text(text).foregroundStyle(DeskhubPalette.muted)
}

@MainActor
func deskhubHeadingRow(_ text: String, onRefresh: @MainActor @escaping () -> Void) -> some View {
    HStack(spacing: 8) {
        deskhubHeading(text)
        Spacer(minLength: 0)
        Button(DeskhubClient.string(DHStrRefreshNow), action: onRefresh)
    }
}
