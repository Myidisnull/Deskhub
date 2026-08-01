import SwiftUI

struct ConnectView: View {
    @Bindable var model: SessionModel

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            TextField("Host IP address", text: $model.connect.address)
                .textFieldStyle(.roundedBorder)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .keyboardType(.numbersAndPunctuation)
                .submitLabel(.go)
                .onSubmit(model.beginConnect)
                .disabled(model.connect.isConnecting)

            HStack(spacing: 12) {
                Button("Connect", action: model.beginConnect)
                    .buttonStyle(.borderedProminent)
                    .disabled(model.connect.address.isEmpty || model.connect.isConnecting)

                if model.connect.isConnecting {
                    ProgressView()
                }
            }

            if !model.connect.connectError.isEmpty {
                Text(model.connect.connectError)
                    .foregroundStyle(.red)
                    .fixedSize(horizontal: false, vertical: true)
            }

            Spacer()
        }
        .padding()
    }
}
