// =============================================================================
// ConnectView.swift — ô nhập địa chỉ + nút Connect (vai CLIENT).
//                     Dựng theo `DesktopConnect` trong desktop.jsx; đối ứng
//
// Ô ĐỊA CHỈ LÀ TRUNG TÂM MÀN HÌNH, KHÔNG PHẢI MỘT DÒNG TRONG FORM
//   Cao 66px, chữ mono 22px, căn giữa theo chiều dọc. Màn này có đúng một việc, và
//   bản thiết kế nói điều đó bằng kích thước. Ba panel bên dưới là nội dung TRẢ LỜI
//   câu "tôi lấy địa chỉ ở đâu" — chúng đứng dưới vì chúng là chú thích, không phải
//   bước tiếp theo.
//
// Địa chỉ được nhớ lại cho lần sau (SessionModel + Recents). Cổng mặc định 47777 do
// tầng C++ điền khi chuỗi không có ":port" (ParseNetAddr) — Swift không lặp lại hằng
// số đó để hai nơi không lệch nhau.
// =============================================================================
import SwiftUI

struct ConnectView: View {
    @Binding var route: Route
    @Bindable var model: SessionModel

    var body: some View {
        ScreenBody(spacing: 26) {
            ScreenHeader(
                eyebrow: tr("clientMode"),
                title: tr("connectTitle"),
                size: DS.textDisplay
            )

            VStack(alignment: .leading, spacing: 16) {
                HeroField(
                    icon: "terminal",
                    text: $model.address,
                    placeholder: tr("addressPlaceholder"),
                    onSubmit: connect
                ) {
                    Button(action: connect) {
                        if model.isConnecting {
                            Spinner(size: 15)
                                .frame(width: 60)
                        } else {
                            Text(tr("connect"))
                        }
                    }
                    .buttonStyle(DSButtonStyle(variant: .primary))
                    .disabled(model.address.isEmpty || model.isConnecting)
                }
                .frame(maxWidth: 700, alignment: .leading)

                MonoText(text: model.isConnecting ? tr("askingSources") : tr("connectHint"))

                if !model.connectError.isEmpty {
                    MonoText(text: model.connectError, color: DS.statusError)
                        .fixedSize(horizontal: false, vertical: true)
                        .frame(maxWidth: 560, alignment: .leading)
                }
            }

            EvenGrid(columns: 3) {
                Panel(label: tr("onOtherMachine")) {
                    VStack(alignment: .leading, spacing: 10) {
                        Text(tr("openAndShare"))
                            .font(DS.ui(DS.textBody, weight: .medium))
                            .foregroundStyle(DS.textPrimary)
                            .fixedSize(horizontal: false, vertical: true)
                        MonoText(text: tr("shareOrExe"))
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
                Panel(label: tr("whereAddress")) {
                    VStack(alignment: .leading, spacing: 10) {
                        Text(tr("printedOnShare"))
                            .font(DS.ui(DS.textBody, weight: .medium))
                            .foregroundStyle(DS.textPrimary)
                            .fixedSize(horizontal: false, vertical: true)
                        MonoText(text: tr("onePerInterfaceLong"))
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
                Panel(label: tr("port")) {
                    HStack(alignment: .bottom, spacing: 22) {
                        StatBlock(label: tr("udp"), value: "47777")
                        MonoText(text: tr("portChangeable"))
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
            }
        } bar: {
            Toggle(isOn: $model.viewOnly) { Text(tr("viewOnlyOption")) }
                .toggleStyle(DSCheckboxStyle())
            Spacer()
            Button(tr("connect"), action: connect)
                .buttonStyle(DSButtonStyle(variant: .primary, size: .lg))
                .disabled(model.address.isEmpty || model.isConnecting)
        }
    }

    // Danh sách rỗng gộp hai trường hợp — host im lặng (bản cũ / mất gói) và host
    // không chia sẻ gì — thành một: cứ vào NGUỒN 0 và để ClientSession báo lỗi thật.
    // Một nguồn thì bỏ qua luôn màn chọn nguồn: bắt người dùng bấm một cái không có
    // lựa chọn nào là một bước thừa.
    private func connect() {
        guard !model.address.isEmpty, !model.isConnecting else { return }
        Task {
            let sources = await model.listSources()
            Recents.remember(address: model.address)
            if sources.count > 1 {
                route = .sourcePicker(sources)
            } else {
                model.startStream(sourceId: sources.first?.id ?? 0)
                route = .stream
            }
        }
    }
}
