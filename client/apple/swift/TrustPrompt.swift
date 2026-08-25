import SwiftUI

extension View {
    func trustPrompt(
        isPresented: Binding<Bool>, changed: Bool, fingerprint: String,
        onAnswer: @escaping (Bool) -> Void
    ) -> some View {
        alert(
            DeskhubClient.string(changed ? DHStrTrustChangedTitle : DHStrTrustNewHostTitle),
            isPresented: isPresented
        ) {
            Button(DeskhubClient.string(DHStrTrustAccept)) { onAnswer(true) }
            Button(DeskhubClient.string(DHStrTrustReject), role: .cancel) { onAnswer(false) }
        } message: {
            Text(
                DeskhubClient.string(changed ? DHStrTrustChangedBody : DHStrTrustNewHostBody)
                    + "\n\n" + DeskhubClient.string(DHStrTrustFingerprintLabel) + " "
                    + fingerprint
            )
        }
    }
}
