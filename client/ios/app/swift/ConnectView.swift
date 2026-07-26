// =============================================================================
// ConnectView.swift — ô nhập địa chỉ + danh sách máy đã nhớ (vai CLIENT).
//                     Dựng theo `DesktopConnect` trong desktop.jsx; đối ứng
//                     client/macos/app/swift/ConnectView.swift và MainActivity bên
//                     Android.
//
// Ô ĐỊA CHỈ LÀ TRUNG TÂM MÀN HÌNH, KHÔNG PHẢI MỘT DÒNG TRONG FORM
//   Cao 58, chữ mono 17. Màn này có đúng một việc, và bản thiết kế nói điều đó bằng
//   kích thước. Mọi thứ dưới nó là nội dung TRẢ LỜI câu "tôi lấy địa chỉ ở đâu" —
//   chúng đứng dưới vì chúng là chú thích, không phải bước tiếp theo.
//
// "GẦN ĐÂY" ĐỨNG NGAY DƯỚI Ô NHẬP, TRÊN CẢ BA PANEL HƯỚNG DẪN
//   Trên desktop ba panel kia nằm cùng một hàng nên không tranh chỗ với gì cả. Ở đây
//   chúng xếp dọc, và lần mở app thứ hai trở đi thì thứ người dùng cần là một cú chạm
//   vào cái máy hôm qua vừa dùng — chứ không phải đọc lại hướng dẫn.
//
// Cổng mặc định 47777 do tầng C++ điền khi chuỗi không có ":port" (ParseNetAddr) —
// Swift không lặp lại hằng số đó để hai nơi không lệch nhau.
// =============================================================================
import SwiftUI

struct ConnectView: View {
    @Bindable var model: SessionModel

    @State private var recents: [RecentMachine] = []
    // GĐ10 — mật khẩu/token đã lưu, cho mục "Saved passwords" bên dưới.
    @State private var savedCreds: [HostCredential] = []

    var body: some View {
        ScreenBody {
            ScreenHeader(eyebrow: tr("clientMode"), title: tr("connectTitle"))

            VStack(alignment: .leading, spacing: 12) {
                HeroField(
                    icon: "terminal",
                    text: $model.address,
                    placeholder: tr("addressPlaceholder"),
                    onSubmit: model.connect
                ) {
                    if model.isConnecting { Spinner(size: 18) }
                }

                MonoText(text: model.isConnecting ? tr("askingSources") : tr("connectHint"))
                    .fixedSize(horizontal: false, vertical: true)

                if !model.connectErrorKey.isEmpty {
                    MonoText(text: tr(model.connectErrorKey), color: DS.statusError)
                        .fixedSize(horizontal: false, vertical: true)
                }
            }

            recentSection
            savedPasswordSection
            helpPanels
        } bar: {
            Toggle(isOn: $model.viewOnly) { Text(tr("viewOnlyOption")) }
                .toggleStyle(DSCheckboxStyle())

            Button(action: model.connect) {
                Text(tr("connect"))
            }
            .buttonStyle(DSButtonStyle(variant: .primary, size: .lg, fullWidth: true))
            .disabled(model.address.isEmpty || model.isConnecting)
        }
        .onAppear {
            recents = Recents.all
            savedCreds = model.savedCredentials
        }
    }

    private var recentSection: some View {
        VStack(alignment: .leading, spacing: 10) {
            SectionHeader(label: tr("recentConnections")) {
                Button(tr("forgetAll")) {
                    Recents.forgetAll()
                    recents = Recents.all
                }
                .buttonStyle(DSButtonStyle(variant: .ghost, size: .sm))
                .disabled(recents.isEmpty)
            }

            if recents.isEmpty {
                MonoText(text: tr("nothingRemembered"))
            } else {
                ForEach(recents) { machine in
                    // Máy đã lưu: ta chưa dò lại nên KHÔNG khẳng định nó đang sống.
                    // Chấm để trạng thái "không sống" thay vì bịa ra một dấu hiệu.
                    MachineCard(
                        name: machine.displayName,
                        address: machine.address,
                        link: machine.link,
                        live: false
                    ) { model.address = machine.address }
                }
            }
        }
    }

    /// GĐ10 — "Saved passwords" của màn `05 · settings / password`.
    ///
    /// Đặt ở đây thay vì dựng một màn Settings riêng: chỉ có đúng một việc để làm
    /// (xoá), và nó thuộc cùng một câu hỏi với danh sách trên ("máy nào tôi đã dùng").
    /// Ẩn hẳn khi chưa lưu gì — một mục rỗng chỉ làm màn hình dài thêm.
    @ViewBuilder
    private var savedPasswordSection: some View {
        if !savedCreds.isEmpty {
            VStack(alignment: .leading, spacing: 10) {
                SectionHeader(label: tr("savedHosts")) {
                    MonoText(text: String(format: tr("savedCountFmt"), savedCreds.count))
                    Button(tr("forgetAll")) {
                        model.forgetAllCredentials()
                        savedCreds = model.savedCredentials
                    }
                    .buttonStyle(DSButtonStyle(variant: .ghost, size: .sm))
                }

                ForEach(savedCreds) { cred in
                    HStack(spacing: 10) {
                        Image(systemName: "lock")
                            .font(.system(size: 16))
                            .foregroundStyle(DS.accent)
                        VStack(alignment: .leading, spacing: 2) {
                            MonoText(text: cred.address, color: DS.textPrimary)
                            // Nói rõ máy này đang được nhớ bằng CÁI GÌ: có token thì
                            // lần sau vào thẳng, chỉ có mật khẩu thì vẫn qua challenge.
                            MonoText(text: cred.hasToken ? tr("savePassword") : tr("connectPassword"))
                        }
                        Spacer(minLength: 8)
                        Button(tr("forget")) {
                            model.forgetCredential(cred.address)
                            savedCreds = model.savedCredentials
                        }
                        .buttonStyle(DSButtonStyle(variant: .secondary, size: .sm))
                    }
                    .padding(.horizontal, 14)
                    .padding(.vertical, 10)
                    .background(DS.surfaceCard, in: RoundedRectangle(cornerRadius: DS.radiusMd, style: .continuous))
                    .overlay(
                        RoundedRectangle(cornerRadius: DS.radiusMd, style: .continuous)
                            .strokeBorder(DS.borderHairline, lineWidth: DS.hairline)
                    )
                }
            }
        }
    }

    private var helpPanels: some View {
        VStack(alignment: .leading, spacing: 12) {
            Panel(label: tr("onOtherMachine")) {
                VStack(alignment: .leading, spacing: 8) {
                    Text(tr("openAndShare"))
                        .font(DS.ui(DS.textBody, weight: .medium))
                        .foregroundStyle(DS.textPrimary)
                        .fixedSize(horizontal: false, vertical: true)
                    MonoText(text: tr("shareOrExe"))
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            Panel(label: tr("whereAddress")) {
                VStack(alignment: .leading, spacing: 8) {
                    Text(tr("printedOnShare"))
                        .font(DS.ui(DS.textBody, weight: .medium))
                        .foregroundStyle(DS.textPrimary)
                        .fixedSize(horizontal: false, vertical: true)
                    MonoText(text: tr("onePerInterfaceLong"))
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
            Panel(label: tr("port")) {
                HStack(alignment: .bottom, spacing: 18) {
                    StatBlock(label: tr("udp"), value: "47777")
                    MonoText(text: tr("portChangeable"))
                        .fixedSize(horizontal: false, vertical: true)
                }
            }
        }
    }
}
