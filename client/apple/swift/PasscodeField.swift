import SwiftUI

struct PasscodeField: View {
    @Binding var passcode: String
    var prompt = ""
    var width: CGFloat?
    var enabled = true
    var onSubmit: () -> Void = {}

    var body: some View {
        #if os(iOS)
            field.keyboardType(.numberPad)
        #else
            field
        #endif
    }

    private var field: some View {
        TextField(prompt, text: $passcode)
            .textFieldStyle(.roundedBorder)
            .frame(width: width)
            .help(DeskhubClient.string(DHStrClientPasscodeHint))
            .disabled(!enabled)
            .onSubmit(onSubmit)
            .onChange(of: passcode) { _, typed in
                let accepted = DeskhubClient.passcodeInput(typed)
                if accepted != typed { passcode = accepted }
            }
    }
}
