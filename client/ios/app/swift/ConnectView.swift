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
        .onAppear { recents = Recents.all }
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
