import SwiftUI

struct TransferRequest: Codable, Hashable {
    var address: String
    var passcode: String
    var name: String
}

struct FileTransferWindow: View {
    @State private var sender = FileSendModel()
    @Environment(\.dismiss) private var dismiss
    private let request: TransferRequest

    init(request: TransferRequest) {
        self.request = request
    }

    var body: some View {
        FileShareView(model: sender) { dismiss() }
            .navigationTitle(DeskhubClient.string(DHStrTransferSendHeading))
            .navigationSubtitle(request.address)
            .onAppear {
                sender.address = request.address
                sender.passcode = request.passcode
                sender.deviceName = request.name
            }
            .onDisappear { sender.forgetTransfer() }
    }
}
