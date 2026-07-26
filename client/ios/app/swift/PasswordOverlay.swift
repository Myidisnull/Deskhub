// =============================================================================
// PasswordOverlay.swift — GĐ10: ô nhập mật khẩu, hiện khi host đòi mà máy này
//                         chưa có. Tách khỏi StreamView vì nó tự quản trọn state
//                         của mình (chuỗi đang gõ, hiện/ẩn, ghi nhớ) — StreamView
//                         chỉ cần biết "phase == .needPassword thì hiện".
//
// Ứng với màn `05 · settings / password` của thiết kế, phần "Password to
// connect". Mật khẩu KHÔNG đi lên dây: tầng C++ đổi nó thành một proof HMAC theo
// challenge của host (docs/04-protocol.md §5.1), nên chuỗi này không rời khỏi máy.
// =============================================================================
import SwiftUI

struct PasswordOverlay: View {
    @Bindable var model: SessionModel
    @State private var passwordInput = ""
    @State private var revealPassword = false
    @State private var rememberPassword = true

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Eyebrow(text: trU("securityEyebrow"))
            Text(tr("connectPassword"))
                .font(DS.ui(DS.textBodyLg, weight: .semibold))
                .foregroundStyle(DS.textPrimary)
            MonoText(text: model.address)

            HStack(spacing: 10) {
                Image(systemName: "lock")
                    .font(.system(size: 16))
                    .foregroundStyle(DS.accent)
                Group {
                    if revealPassword {
                        TextField(tr("connectPassword"), text: $passwordInput)
                    } else {
                        SecureField(tr("connectPassword"), text: $passwordInput)
                    }
                }
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .font(DS.mono(16))
                .foregroundStyle(DS.textPrimary)
                .submitLabel(.go)
                .onSubmit(submitPassword)
                Button {
                    revealPassword.toggle()
                } label: {
                    Image(systemName: revealPassword ? "eye.slash" : "eye")
                        .font(.system(size: 15))
                        .foregroundStyle(DS.textSecondary)
                }
            }
            .padding(.horizontal, 14)
            .frame(height: DS.fieldHeight)
            .background(DS.surfaceField, in: RoundedRectangle(cornerRadius: DS.radiusMd, style: .continuous))
            .overlay(
                RoundedRectangle(cornerRadius: DS.radiusMd, style: .continuous)
                    .strokeBorder(DS.borderHairline, lineWidth: DS.hairline)
            )

            Toggle(tr("savePassword"), isOn: $rememberPassword)
                .font(DS.ui(DS.textBody))
                .foregroundStyle(DS.textPrimary)
            if Credentials.biometricAvailable {
                Toggle(tr("biometricUnlock"), isOn: biometricBinding)
                    .font(DS.ui(DS.textBody))
                    .foregroundStyle(DS.textPrimary)
            }
            MonoText(text: tr("passwordHintPhone"))

            Button(tr("connect"), action: submitPassword)
                .buttonStyle(DSButtonStyle(variant: .primary, fullWidth: true))
                .disabled(passwordInput.isEmpty)
            Button(tr("back")) { model.disconnect() }
                .buttonStyle(DSButtonStyle(variant: .secondary, fullWidth: true))
        }
        .frame(maxWidth: 360, alignment: .leading)
        .padding(22)
        .background(DS.surfacePanel, in: RoundedRectangle(cornerRadius: DS.radiusXl, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: DS.radiusXl, style: .continuous)
                .strokeBorder(DS.borderHairline, lineWidth: DS.hairline)
        )
        .padding(.horizontal, 24)
    }

    // Ô "Unlock with Face ID" nằm ngoài SwiftUI state (nó là preference toàn app,
    // lưu ở UserDefaults) nên phải bắc cầu bằng Binding thủ công.
    private var biometricBinding: Binding<Bool> {
        Binding(
            get: { Credentials.biometricEnabled },
            set: { Credentials.biometricEnabled = $0 }
        )
    }

    private func submitPassword() {
        guard !passwordInput.isEmpty else { return }
        model.submitPassword(passwordInput, remember: rememberPassword)
        passwordInput = ""
        revealPassword = false
    }
}
