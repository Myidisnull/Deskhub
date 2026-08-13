import SwiftUI

struct SharingView: View {
    @Bindable var model: SharingModel

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                deskhubHeading(DeskhubClient.string(DHStrHostHeading))

                Text(
                    DeskhubClient.string(
                        model.status.sharing ? DHStrShareStateOn : DHStrShareStateOff
                    )
                )
                .font(.system(size: 15, weight: .semibold))
                .foregroundStyle(
                    model.status.sharing ? DeskhubPalette.online : DeskhubPalette.muted
                )

                deskhubSection(DeskhubClient.string(DHStrPasscodeLabel))
                PasscodeField(
                    passcode: $model.passcode,
                    prompt: DeskhubClient.string(DHStrClientPasscodePrompt),
                    width: 140,
                    enabled: !model.status.sharing
                )
                .onChange(of: model.passcode) { _, _ in model.savePasscode() }

                BroadcastPickerButton(
                    extensionBundleId: SharingModel.extensionBundleId,
                    title: DeskhubClient.string(
                        model.status.sharing ? DHStrStopSharing : DHStrStartSharing
                    )
                )

                deskhubHint(model.statusLine)
                if model.status.sharing, model.status.memoryMB > 0 {
                    deskhubHint(
                        "\(DeskhubClient.string(DHStrBroadcastMemoryLabel)): "
                            + "\(model.status.memoryMB) MB"
                    )
                }
                if !model.status.error.isEmpty {
                    Text(model.status.error).foregroundStyle(DeskhubPalette.offline)
                }

                deskhubSection(DeskhubClient.string(DHStrHostIpIntro))
                if model.addresses.isEmpty {
                    deskhubHint(DeskhubClient.string(DHStrNoNetworkAddress))
                } else {
                    ForEach(model.addresses) { address in
                        HStack(spacing: 12) {
                            Text(address.name).foregroundStyle(DeskhubPalette.muted)
                            Spacer(minLength: 0)
                            Text(address.ip).fontWeight(.bold).textSelection(.enabled)
                        }
                    }
                }

                deskhubHint(DeskhubClient.string(DHStrSharingConnectHint))
                deskhubHint(viewerLine)
            }
            .padding()
        }
        .task { await model.poll() }
    }

    private var viewerLine: String {
        guard model.status.sharing else { return DeskhubClient.string(DHStrNotSharing) }
        guard model.status.viewers > 0 else {
            return DeskhubClient.string(DHStrNothingShared)
        }
        guard !model.status.viewerNames.isEmpty else { return "\(model.status.viewers)" }
        return "\(model.status.viewers): \(model.status.viewerNames)"
    }
}
