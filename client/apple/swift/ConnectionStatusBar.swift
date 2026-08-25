import SwiftUI

enum LinkHealthStyle {
    static func dotColor(_ quality: DHLinkQuality) -> Color {
        switch quality {
        case DHLinkGood: .green
        case DHLinkFair: .orange
        case DHLinkPoor: .red
        default: .gray
        }
    }
}

struct LinkHealthRow: View {
    let health: ClientSession.LinkHealth

    var body: some View {
        HStack(spacing: 6) {
            Circle()
                .fill(LinkHealthStyle.dotColor(DHLinkQuality(rawValue: health.quality) ?? DHLinkUnknown))
                .frame(width: 8, height: 8)
            Text(DeskhubClient.linkQualityText(DHLinkQuality(rawValue: health.quality) ?? DHLinkUnknown))
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
