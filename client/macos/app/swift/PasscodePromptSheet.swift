import SwiftUI

struct PasscodePromptSheet: View {
    let address: String
    @Binding var passcode: String
    let onCancel: () -> Void
    let onConnect: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            deskhubHeading(DeskhubClient.string(DHStrConnectPromptTitle))

            Text(address)
                .font(.system(size: 15, weight: .bold))
                .foregroundStyle(DeskhubPalette.heading)

            HStack(spacing: 10) {
                Text(DeskhubClient.string(DHStrClientPasscodePrompt))
                PasscodeField(passcode: $passcode, width: 72, onSubmit: onConnect)
            }

            deskhubHint(DeskhubClient.string(DHStrClientPasscodeHint))

            HStack {
                Spacer(minLength: 0)
                Button("Cancel", role: .cancel, action: onCancel)
                    .keyboardShortcut(.cancelAction)
                Button("Connect", action: onConnect)
                    .keyboardShortcut(.defaultAction)
                    .disabled(!DeskhubClient.isValidPasscode(passcode))
            }
            .padding(.top, 4)
        }
        .padding(20)
        .frame(minWidth: 340, alignment: .leading)
    }
}
