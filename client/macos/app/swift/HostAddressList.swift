import AppKit
import SwiftUI

struct HostAddressList: View {
    let addresses: [LocalAddress]

    var body: some View {
        if addresses.isEmpty {
            Text(DeskhubClient.string(DHStrNoNetworkAddress)).foregroundStyle(.secondary)
        } else {
            ForEach(addresses) { addr in
                HStack(spacing: 14) {
                    Text(addr.name).frame(width: 150, alignment: .leading).lineLimit(1)
                    Text(addr.ip).fontWeight(.bold).textSelection(.enabled)
                    Spacer(minLength: 0)
                    Button("Copy") {
                        NSPasteboard.general.clearContents()
                        NSPasteboard.general.setString(addr.ip, forType: .string)
                    }
                    .frame(width: 84)
                }
            }
        }
    }
}
