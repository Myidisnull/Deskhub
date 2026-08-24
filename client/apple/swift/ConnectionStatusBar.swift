import SwiftUI

enum LinkHealthStyle {
    static func dotColor(_ quality: DHLinkQuality) -> Color {
        switch quality {
        case DHLinkQualityGood: .green
        case DHLinkQualityFair: .orange
        case DHLinkQualityPoor: .red
        default: .gray
        }
    }
}

struct LinkHealthRow: View {
    let health: DHLinkHealth

    var body: some View {
        HStack(spacing: 6) {
            Circle()
                .fill(LinkHealthStyle.dotColor(health.quality))
                .frame(width: 8, height: 8)
            Text(DeskhubClient.linkQualityText(health.quality))
            Text(DeskhubClient.linkPingText(haveRtt: health.haveRtt, rttMs: health.rttMs))
                .foregroundStyle(.secondary)
        }
    }
}

struct ConnectionStatusBar: View {
    let model: StreamModel
    let onDisconnect: () -> Void

    var body: some View {
        HStack(spacing: 10) {
            leading
            Spacer(minLength: 12)
            Text(model.address)
                .foregroundStyle(.secondary)
                .lineLimit(1)
                .truncationMode(.middle)
            Button(DeskhubClient.string(DHStrDisconnectButton), action: onDisconnect)
        }
        .font(.callout)
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
        .background(.bar)
    }

    @ViewBuilder private var leading: some View {
        if model.reattaching {
            ProgressView()
                .controlSize(.small)
            Text(DeskhubClient.string(DHStrLinkReattaching))
        } else {
            LinkHealthRow(health: model.linkHealth)
        }
    }
}
