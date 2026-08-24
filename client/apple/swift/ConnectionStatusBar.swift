import SwiftUI

struct ConnectionStatusBar: View {
    let model: StreamModel
    let onDisconnect: () -> Void

    var body: some View {
        HStack(spacing: 10) {
            if model.reattaching {
                ProgressView()
                    .controlSize(.small)
                Text(DeskhubClient.string(DHStrLinkReattaching))
            }
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
}
