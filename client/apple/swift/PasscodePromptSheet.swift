import SwiftUI

struct PasscodePromptSheet: View {
    let address: String
    @Binding var passcode: String
    let onCancel: () -> Void
    let onConnect: () -> Void

    private var ready: Bool { DeskhubClient.isValidPasscode(passcode) }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            deskhubHeading(DeskhubClient.string(DHStrConnectPromptTitle))

            Text(address)
                .font(.system(size: 15, weight: .bold))
                .foregroundStyle(DeskhubPalette.heading)

            HStack(spacing: 10) {
                Text(DeskhubClient.string(DHStrClientPasscodePrompt))
                PasscodeField(
                    passcode: $passcode, width: 72, focusOnAppear: true, onSubmit: submit
                )
            }

            deskhubHint(DeskhubClient.string(DHStrClientPasscodeHint))

            HStack {
                Spacer(minLength: 0)
                Button("Cancel", role: .cancel, action: onCancel)
                    .keyboardShortcut(.cancelAction)
                Button("Connect", action: submit)
                    .keyboardShortcut(.defaultAction)
                    .disabled(!ready)
            }
            .padding(.top, 4)
        }
        .padding(20)
        .frame(minWidth: 340, alignment: .leading)
        #if os(iOS)
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .top)
            .presentationDetents([.medium])
        #endif
    }

    private func submit() {
        guard ready else { return }
        onConnect()
    }
}
