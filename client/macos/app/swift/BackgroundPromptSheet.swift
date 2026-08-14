import SwiftUI

struct BackgroundPromptSheet: View {
    @Binding var choiceYes: Bool
    var onConfirm: () -> Void
    var onClose: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            deskhubHeading(DeskhubClient.string(DHStrBackgroundPromptTitle))
            Text(DeskhubClient.string(DHStrBackgroundPromptMessage))
                .foregroundStyle(.primary)

            Picker("", selection: $choiceYes) {
                Text(DeskhubClient.string(DHStrBackgroundPromptYes)).tag(true)
                Text(DeskhubClient.string(DHStrBackgroundPromptNo)).tag(false)
            }
            .pickerStyle(.radioGroup)
            .labelsHidden()

            HStack {
                Spacer()
                Button(DeskhubClient.string(DHStrBackgroundPromptClose), action: onClose)
                    .keyboardShortcut(.cancelAction)
                Button(DeskhubClient.string(DHStrBackgroundPromptConfirm), action: onConfirm)
                    .keyboardShortcut(.defaultAction)
            }
        }
        .padding(20)
        .frame(width: 380)
    }
}
