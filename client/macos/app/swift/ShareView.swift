// =============================================================================
// ShareView.swift — chọn nguồn, đặt tham số, chia sẻ, VÀ theo dõi phiên đang chạy —
//                   tất cả trên MỘT màn. Dựng theo `DesktopShare` trong desktop.jsx;
//
// VÌ SAO GỘP (trước đây là ShareView + SessionView)
//   Bản thiết kế vẽ chọn nguồn và tình trạng phiên trên cùng một màn, và đó là quyết
//   định đúng: trong lúc đang chia sẻ, việc người ta hay làm nhất là THÊM hoặc BỎ một
//   nguồn. Tách hai màn thì thao tác đó thành "quay lại → chọn lại → bắt đầu lại", mà
//   bắt đầu lại sẽ ngắt phiên của người đang xem.
//
//   Nên danh sách nguồn ở đây có ô tick SỐNG: tick thêm một màn hình là nó vào phiên
//   đang chạy, bỏ tick là nó ra — máy bên kia không rớt kết nối.
//
// HAI TRẠNG THÁI, MỘT MÀN
//   Chưa chia sẻ: tick nguồn, đặt tham số, bấm "Chia sẻ".
//   Đang chia sẻ: tick/bỏ tick tác động NGAY; tham số khoá lại (đổi cổng giữa phiên
//                 nghĩa là dựng lại socket, tức ngắt người xem); hiện thêm "Dừng hết"
//                 và panel máy đang xem.
//
// QUYỀN ĐỨNG TRƯỚC MỌI THỨ KHÁC
//   Thiếu Screen Recording thì danh sách nguồn gần như trống và người dùng không thể
//   đoán vì sao (macOS không báo lỗi — xem agent/Permissions.h). Nên banner quyền nằm
//   trên cùng, và nút Chia sẻ bị khoá khi chưa có quyền: thà chặn rõ ràng còn hơn để
//   họ bấm rồi nhận một thất bại không giải thích được.
//
// ĐỊA CHỈ ĐỌC ĐƯỢC
//   Panel "nhập ở máy bên kia" dùng cỡ chữ mono LỚN. Đây là con số người ta phải đọc
//   to cho người ở phòng khác, hoặc gõ lại bằng tay trên điện thoại.
// =============================================================================
import SwiftUI

struct ShareView: View {
    @Binding var route: Route
    @Bindable var model: AgentModel

    @State private var trace: [Double] = []

    private let fpsOptions = ["30", "60", "120"]
    private let bitrateOptions = ["8 Mbps", "20 Mbps", "40 Mbps"]
    private let portOptions = ["47777", "47778", "52000"]

    var body: some View {
        ScreenBody(topPad: 28, spacing: 20) {
            ScreenHeader(
                eyebrow: tr("hostMode"),
                title: tr("shareTitle"),
                aside: tr("privateAside")
            )

            permissionBanners

            EvenGrid(columns: 2) {
                addressPanel
                viewerPanel
            }

            sourceSection
        } bar: {
            bottomBar
        }
        .task {
            model.refreshPermissions()
            await model.scanSources()
        }
        .onChange(of: model.tick) { _, _ in pushTrace() }
    }

    // MARK: - Quyền

    @ViewBuilder private var permissionBanners: some View {
        if !model.hasScreenRecording {
            PermissionBanner(
                title: tr("screenPermTitle"),
                detail: tr("screenPermBody"),
                actionLabel: tr("screenPermAction")
            ) { DeskhubAgent.openScreenRecordingSettings() }
        } else if model.allowInput, !model.hasAccessibility {
            PermissionBanner(
                title: tr("axPermTitle"),
                detail: tr("axPermBody"),
                actionLabel: tr("axPermAction")
            ) { model.requestAccessibility() }
        }
    }

    // MARK: - Địa chỉ

    // Nhiều địa chỉ chứ không một: máy nào cũng có vài đường ra (Wi-Fi, Ethernet,
    // Tailscale) và chỉ người dùng mới biết máy kia nằm ở nhánh nào — xem net/NetInfo.h.
    private var addressPanel: some View {
        Panel(label: tr("enterOnOther")) {
            VStack(alignment: .leading, spacing: 8) {
                if !model.isSharing {
                    MonoText(text: tr("notSharingYet"))
                } else if model.addresses.isEmpty {
                    MonoText(text: tr("noAddress"), color: DS.statusWarn)
                        .fixedSize(horizontal: false, vertical: true)
                } else {
                    ForEach(model.addresses, id: \.self) { raw in
                        let parts = splitAddress(raw)
                        AddressRow(address: parts.address, link: parts.link)
                    }
                }
            }
        }
    }

    // Địa chỉ từ C++ có dạng "192.168.1.5  (Wi-Fi (en0))" — tách ra để chèn cổng ngay
    // sau IP (người bên kia phải gõ nguyên chuỗi đó) và để tên card mạng đứng riêng
    // như một nhãn, đúng bố cục AddressRow của bản thiết kế.
    private func splitAddress(_ raw: String) -> (address: String, link: String) {
        let parts = raw.split(separator: " ", maxSplits: 1, omittingEmptySubsequences: true)
        guard let ip = parts.first else { return (raw, "") }
        let link = parts.count > 1
            ? parts[1].trimmingCharacters(in: CharacterSet(charactersIn: " ()"))
            : ""
        return ("\(ip):\(model.activePort)", link)
    }

    // MARK: - Máy đang xem

    // Giao thức không mang tên hay địa chỉ của người xem về phía host, nên panel này
    // nói đúng thứ nó biết: CÓ ai đang xem hay không, và luồng đang chạy nhanh cỡ nào.
    // Bịa ra một cái tên máy ở đây sẽ là dòng chữ duy nhất trong app không phải sự thật.
    private var viewerPanel: some View {
        Panel(label: tr("connectedViewer")) {
            if watchedRows.isEmpty {
                VStack(alignment: .leading, spacing: 6) {
                    Text(tr("noViewer"))
                        .font(DS.ui(DS.textBody))
                        .foregroundStyle(DS.textPrimary)
                    MonoText(text: tr("noViewerHint"))
                        .fixedSize(horizontal: false, vertical: true)
                }
            } else {
                VStack(alignment: .leading, spacing: 14) {
                    HStack(spacing: 12) {
                        StatusDot(live: true)
                        VStack(alignment: .leading, spacing: 4) {
                            Text(watchedRows.first?.name ?? "")
                                .font(DS.ui(DS.textBodyLg, weight: .semibold))
                                .foregroundStyle(DS.textPrimary)
                                .lineLimit(1)
                            MonoText(text: model.allowInput ? tr("keysAndMouse") : tr("viewOnly"))
                        }
                        Spacer(minLength: 12)
                        Button(tr("disconnect")) { stopAll() }
                            .buttonStyle(DSButtonStyle(variant: .danger, size: .sm, pill: true))
                    }

                    HStack(alignment: .bottom, spacing: 26) {
                        StatBlock(
                            label: tr("sending"),
                            value: String(format: "%.0f", totalSendFps),
                            unit: tr("fps"),
                            accent: true
                        )
                        StatBlock(
                            label: tr("bitrate"),
                            value: String(format: "%.1f", totalSendKbps / 1000),
                            unit: "Mbps"
                        )
                        StatBlock(
                            label: tr("capture"),
                            value: String(format: "%.0f", maxCaptureFps),
                            unit: tr("fps")
                        )
                    }

                    // Bản thiết kế vẽ biểu đồ độ trễ ở đây, nhưng phía HOST không đo
                    // được RTT (chỉ client đo — xem ClientLoop.cpp). Vẽ nhịp GỬI thay
                    // vào: cùng một câu hỏi ("luồng có đều không?"), và là số liệu
                    // host thật sự có.
                    Sparkline(values: trace)
                        .frame(height: 30)
                }
            }
        }
    }

    private var watchedRows: [AgentSourceStatus] {
        model.rows.filter(\.viewerConnected)
    }

    private var totalSendFps: Double { watchedRows.reduce(0) { $0 + $1.sendFps } }
    private var totalSendKbps: Double { watchedRows.reduce(0) { $0 + $1.sendKbps } }
    private var maxCaptureFps: Double { watchedRows.map(\.captureFps).max() ?? 0 }

    private func pushTrace() {
        guard model.isSharing else {
            trace = []
            return
        }
        trace.append(totalSendFps)
        if trace.count > 60 { trace.removeFirst(trace.count - 60) }
    }

    // MARK: - Danh sách nguồn

    private var sourceSection: some View {
        VStack(alignment: .leading, spacing: 10) {
            SectionHeader(label: tr("displays")) {
                if model.isScanning { Spinner() }
                MonoText(text: String(
                    format: tr("sharedCountFmt"),
                    model.selected.count,
                    model.available.count
                ))
                Button {
                    Task { await model.scanSources() }
                } label: {
                    Label(tr("refresh"), systemImage: "arrow.clockwise")
                }
                .buttonStyle(DSButtonStyle(variant: .secondary, size: .sm))
                .disabled(model.isScanning)
            }

            if model.available.isEmpty {
                MonoText(text: tr("noSources"))
            } else {
                EvenGrid(columns: 2, spacing: 10) {
                    ForEach(model.available) { source in
                        let live = liveRow(for: source)
                        SourceRow(
                            name: source.name,
                            detail: "\(source.width)×\(source.height)",
                            selected: model.selected.contains(source.id),
                            state: stateLabel(live: live),
                            tone: live == nil ? .neutral : .live,
                            onToggle: { on in toggle(source, on: on) },
                            stopLabel: live == nil ? nil : tr("stop"),
                            onStop: live.map { row in { model.removeSource(id: row.id) } }
                        )
                    }
                }
            }

            if !model.startError.isEmpty {
                MonoText(text: model.startError, color: DS.statusError)
                    .fixedSize(horizontal: false, vertical: true)
            }
        }
    }

    // Nối một nguồn trong danh sách quét với dòng trạng thái của phiên bằng TÊN: tầng
    // C++ không trả sourceId nó vừa cấp cho dha_add_source, và tên trong AgentSourceStatus
    // chính là chuỗi ta đã gửi xuống, nên nó khớp nguyên văn.
    private func liveRow(for source: ShareSource) -> AgentSourceStatus? {
        model.rows.first { $0.name == source.name }
    }

    // Pill trạng thái CHỈ xuất hiện khi đã có phiên. Trước lúc bấm "Chia sẻ" thì không
    // nguồn nào đang chạy, nên gắn "IDLE" cho cả danh sách là một cột chữ lặp lại không
    // nói thêm gì — dấu tick đã nói xong việc "cái này sẽ được chia sẻ".
    private func stateLabel(live: AgentSourceStatus?) -> String {
        guard model.isSharing else { return "" }
        guard let live else { return tr("idle") }
        return live.starting ? tr("starting") : tr("streaming")
    }

    // Tick giữa phiên đi thẳng vào phiên đang chạy; tick lúc chưa chia sẻ chỉ đổi danh
    // sách chờ. Cùng một cử chỉ, hai hệ quả — và đó là điều bản thiết kế muốn: người
    // dùng không phải học hai cách làm cho cùng một việc.
    private func toggle(_ source: ShareSource, on: Bool) {
        if on {
            model.selected.insert(source.id)
            if model.isSharing { model.addSource(source) }
        } else {
            model.selected.remove(source.id)
            if let row = liveRow(for: source) { model.removeSource(id: row.id) }
        }
    }

    // MARK: - Thanh dưới

    @ViewBuilder private var bottomBar: some View {
        MonoText(text: "fps")
        DSSelect(options: fpsOptions, value: fpsBinding, width: 92)
            .disabled(model.isSharing)
        MonoText(text: "bitrate")
        DSSelect(options: bitrateOptions, value: bitrateBinding, width: 128)
            .disabled(model.isSharing)
        MonoText(text: tr("port").lowercased())
        DSSelect(options: portOptions, value: $model.port, width: 104)
            .disabled(model.isSharing)

        Spacer()

        Toggle(isOn: $model.allowInput) { Text(tr("allowInput")) }
            .toggleStyle(DSCheckboxStyle())
            .disabled(model.isSharing)


        // Hai mức dừng, đúng như bản thiết kế xếp cạnh nhau:
        //   Dừng     — tắt phiên, GIỮ nguyên các tick. Bấm "Chia sẻ" lại là chạy tiếp
        //              với đúng bộ nguồn đó.
        //   Dừng hết — tắt phiên VÀ bỏ hết tick, về màn trắng.
        // Chỉ cái thứ hai mang màu danger vì chỉ nó làm mất công người ta đã bỏ ra để
        // chọn nguồn.
        if model.isSharing {
            Button(tr("stop")) { model.stopSharing() }
                .buttonStyle(DSButtonStyle(variant: .secondary))
            Button(tr("stopAll")) { stopAll() }
                .buttonStyle(DSButtonStyle(variant: .danger))
        } else {
            Button {
                Task { await model.startSharing() }
            } label: {
                if model.isStarting {
                    Spinner(size: 16).frame(width: 52)
                } else {
                    Text(tr("share"))
                }
            }
            .buttonStyle(DSButtonStyle(variant: .primary, size: .lg))
            .disabled(model.selected.isEmpty || model.isStarting || !model.hasScreenRecording)
        }
    }

    private func stopAll() {
        model.stopSharing()
        model.selected.removeAll()
        trace = []
    }

    // Model giữ fps/bitrate là số (nó gửi thẳng xuống C++); ô chọn hiện chuỗi có đơn
    // vị. Hai binding này là chỗ DUY NHẤT dịch qua lại, thay vì rải String(...) ở mọi
    // nơi đọc giá trị.
    private var fpsBinding: Binding<String> {
        Binding(get: { String(model.fps) }, set: { model.fps = Int($0) ?? 60 })
    }

    private var bitrateBinding: Binding<String> {
        Binding(
            get: { "\(model.bitrateMbps) Mbps" },
            set: { model.bitrateMbps = Int($0.split(separator: " ").first ?? "20") ?? 20 }
        )
    }
}
