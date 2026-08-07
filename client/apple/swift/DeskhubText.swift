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
