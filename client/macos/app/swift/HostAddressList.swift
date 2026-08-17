import AppKit
import SwiftUI

struct HostAddressList: View {
    let addresses: [LocalAddress]
    var staleIp: String?
    @State private var copiedIp: String?

    var body: some View {
        if addresses.isEmpty {
            if let staleIp {
                Text("\(staleIp)  (\(DeskhubClient.string(DHStrBindNotConnectedNote)))")
                    .foregroundStyle(.secondary)
            } else {
                Text(DeskhubClient.string(DHStrNoNetworkAddress)).foregroundStyle(.secondary)
            }
        } else {
            ForEach(addresses) { addr in
                HStack(spacing: 14) {
                    Text(addr.name).frame(width: 150, alignment: .leading).lineLimit(1)
                    Text(addr.ip).fontWeight(.bold).textSelection(.enabled)
                    Spacer(minLength: 0)
                    Button(
                        copiedIp == addr.ip
                            ? DeskhubClient.string(DHStrCopied)
                            : DeskhubClient.string(DHStrCopy)
                    ) {
                        NSPasteboard.general.clearContents()
                        NSPasteboard.general.setString(addr.ip, forType: .string)
                        copiedIp = addr.ip
                    }
                    .frame(width: 84)
                }
            }
            .task(id: copiedIp) {
                guard copiedIp != nil else { return }
                try? await Task.sleep(for: .seconds(1.5))
                copiedIp = nil
            }
        }
    }
}
