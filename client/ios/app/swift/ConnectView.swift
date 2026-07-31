import SwiftUI

struct ConnectView: View {
    @Bindable var model: SessionModel

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            TextField("Host IP address", text: $model.address)
                .textFieldStyle(.roundedBorder)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .keyboardType(.numbersAndPunctuation)
                .submitLabel(.go)
                .onSubmit(model.connect)
                .disabled(model.isConnecting)

            HStack(spacing: 12) {
                Button("Connect", action: model.connect)
                    .buttonStyle(.borderedProminent)
                    .disabled(model.address.isEmpty || model.isConnecting)

                if model.isConnecting {
                    ProgressView()
                }
            }

            if !model.connectError.isEmpty {
                Text(model.connectError)
                    .foregroundStyle(.red)
                    .fixedSize(horizontal: false, vertical: true)
            }

            Spacer()
        }
        .padding()
    }
}
