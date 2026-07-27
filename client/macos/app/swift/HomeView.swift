// =============================================================================
// HomeView.swift — màn chính. Dựng theo `DesktopHome` trong desktop.jsx:
//   tiêu đề → hai ô lớn (Kết nối / Chia sẻ) → "Kết nối gần đây"
//   → thanh hành động dưới cùng.
//
// HAI VIỆC MỘT MÀN HÌNH (docs/01 §1): "máy này cho người khác
// dùng" và "tôi muốn dùng máy khác". Không đoán ý người dùng, không nhớ lựa chọn lần
// trước — chọn sai vai trên một app điều khiển từ xa khó chịu hơn nhiều so với bấm
// thêm một nút.
//
// KHÔNG CÓ MỤC "TÌM THẤY TRONG MẠNG"
//   LAN discovery (DISCOVER/ANNOUNCE) đã gỡ toàn dự án 2026-07-27 — kết nối bằng
//   địa chỉ gõ tay đọc từ màn chia sẻ của máy kia.
//
// BẤM MỘT THẺ MÁY = MỞ MÀN KẾT NỐI VỚI ĐỊA CHỈ ĐIỀN SẴN, KHÔNG NỐI THẲNG
//   Nối thẳng sẽ bỏ qua ô "chỉ xem", mà đó là lựa chọn người ta cần cân nhắc trước khi
//   gõ vào máy người khác.
// =============================================================================
import SwiftUI

struct HomeView: View {
    @Binding var route: Route
    @Bindable var agent: AgentModel
    @Bindable var session: SessionModel

    @State private var recents: [RecentMachine] = []

    var body: some View {
        ScreenBody {
            ScreenHeader(
                eyebrow: tr("machines"),
                title: tr("takeDesk"),
                aside: tr("oneApp")
            )

            EvenGrid(columns: 2) {
                Tile(
                    icon: "video",
                    title: tr("connectTitle"),
                    detail: tr("connectBody"),
                    action: tr("takeControl")
                ) { route = .connect }

                Tile(
                    icon: "square.on.square",
                    title: tr("shareTitle"),
                    detail: tr("shareBody"),
                    action: tr("startSharing")
                ) {
                    agent.refreshPermissions()
                    route = .share
                }
            }

            VStack(alignment: .leading, spacing: 14) {
                SectionHeader(label: tr("recentConnections")) {
                    Button {
                        Recents.forgetAll()
                        recents = Recents.all
                    } label: {
                        Label(tr("forgetAll"), systemImage: "trash")
                            .labelStyle(.titleAndIcon)
                    }
                    .buttonStyle(DSButtonStyle(variant: .secondary, size: .sm))
                    .disabled(recents.isEmpty)
                }

                if recents.isEmpty {
                    MonoText(text: tr("nothingRemembered"))
                } else {
                    EvenGrid(columns: 3) {
                        ForEach(recents) { machine in
                            // Máy đã lưu: ta chưa dò lại nên KHÔNG khẳng định nó đang
                            // sống. Chấm để trạng thái "không sống" và không hiện độ
                            // trễ bịa ra.
                            MachineCard(
                                name: machine.displayName,
                                address: machine.address,
                                link: machine.link,
                                live: false,
                                layout: .stacked
                            ) { open(machine.address) }
                        }
                    }
                }
            }
        } bar: {
            MonoText(text: String(format: tr("reachableFmt"), recents.count))
            Spacer()
            Button {
                agent.refreshPermissions()
                route = .share
            } label: {
                Label(tr("shareThisMac"), systemImage: "square.on.square")
            }
            .buttonStyle(DSButtonStyle(variant: .secondary))

            Button { route = .connect } label: {
                Label(tr("connect"), systemImage: "video")
            }
            .buttonStyle(DSButtonStyle(variant: .primary))
        }
        .onAppear {
            recents = Recents.all
            agent.refreshPermissions()
        }
    }

    private func open(_ address: String) {
        session.address = address
        route = .connect
    }
}
