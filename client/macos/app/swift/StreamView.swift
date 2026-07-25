// =============================================================================
// StreamView.swift — màn hình XEM + ĐIỀU KHIỂN. Dựng theo `DesktopViewer` trong
//                    desktop.jsx; đối ứng client/windows/csharp/Views/ViewerPage.xaml.
//
// KHÔNG CÓ THANH CÔNG CỤ, CHỈ CÓ HAI HUD NỔI
//   Màn cũ có một thanh số liệu đặc chiếm nguyên một hàng trên cùng. Bản thiết kế bỏ
//   hẳn nó: hình từ máy kia chiếm TRỌN cửa sổ, số liệu nổi ở góc trên phải, điều khiển
//   nổi ở giữa dưới. Một thanh đặc ăn mất chiều cao của thứ duy nhất người ta mở màn
//   này ra để nhìn.
//
// NỀN LUÔN ĐEN, KỂ CẢ Ở GIAO DIỆN SÁNG
//   Vùng letterbox quanh khung hình phải là màu KHÔNG CÓ, chứ không phải một màu nhạt
//   — nếu không, mắt sẽ đọc nó thành một phần của hình. Vì vậy cả màn này ép
//   colorScheme = .dark: mọi thứ nổi trên video giữ bảng màu tối, đúng như
//   theme-light.css quy định cho `.om-video`.
//
// KHÁC BẢN iOS: KHÔNG CÓ THANH PHÍM TẮT
//   iOS phải có hàng nút Esc/Tab/mũi tên vì bàn phím ảo không có những phím đó. macOS
//   có bàn phím thật — RemoteView bắt trọn và gửi thẳng, kể cả Esc, Tab, F-key. Ngoại
//   lệ duy nhất là F9: nó bị giữ lại làm phím thoát hiểm cho khoá chuột.
// =============================================================================
import AVFoundation
import SwiftUI

struct StreamView: View {
    @Binding var route: Route
    @Bindable var model: SessionModel

    @State private var fill = false

    var body: some View {
        ZStack {
            DS.bgVideo.ignoresSafeArea()

            RemoteView(
                model: model,
                videoSize: videoSize,
                fill: fill,
                mouseLocked: model.mouseLocked,
                onLayerReady: { layer in DeskhubClient.setLayer(layer) },
                onLockChanged: { model.mouseLocked = $0 }
            )

            hostLabel
            statsHud
            controlHud

            if model.phase != .streaming, model.endReason.isEmpty {
                connectingOverlay
            }
            if !model.endReason.isEmpty {
                endedOverlay
            }
        }
        .environment(\.colorScheme, .dark)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .onDisappear {
            DeskhubClient.setLayer(nil)
            model.disconnect()
        }
    }

    private var videoSize: CGSize {
        CGSize(width: Double(model.videoWidth), height: Double(model.videoHeight))
    }

    // Nhãn máy đang xem, góc trên trái.
    private var hostLabel: some View {
        HStack(spacing: 10) {
            StatusDot(live: model.phase == .streaming)
            MonoText(
                text: hostTitle,
                size: DS.textMono,
                color: DS.textPrimary
            )
            StatePill(
                text: model.phase == .streaming ? tr("streaming") : tr("connecting"),
                tone: model.phase == .streaming ? .live : .neutral
            )
        }
        .padding(26)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .allowsHitTesting(false)
    }

    private var hostTitle: String {
        guard model.videoWidth > 0 else { return model.address }
        return "\(model.address) — \(model.videoWidth)×\(model.videoHeight)"
    }

    // HUD số liệu, góc trên phải.
    private var statsHud: some View {
        HudBar {
            MonoText(
                text: model.statusLine.isEmpty ? tr("connecting") : model.statusLine,
                color: DS.textPrimary
            )
            .lineLimit(1)
            if model.rttTrace.count >= 2 {
                HudDivider()
                Sparkline(values: model.rttTrace)
                    .frame(width: 96, height: 22)
            }
        }
        .padding(.top, 24)
        .padding(.trailing, 24)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topTrailing)
    }

    // HUD điều khiển, giữa dưới.
    private var controlHud: some View {
        HudBar {
            Button {
                model.mouseLocked.toggle()
            } label: {
                Image(systemName: model.mouseLocked ? "cursorarrow.rays" : "cursorarrow")
                    .font(.system(size: 16))
            }
            .buttonStyle(DSIconButtonStyle(side: 34, radius: DS.radiusPill, active: model.mouseLocked))
            .help(tr(model.mouseLocked ? "mouseLocked" : "mouseFree"))
            .disabled(model.viewOnly)

            Button { fill.toggle() } label: {
                Image(systemName: fill ? "arrow.down.right.and.arrow.up.left" : "arrow.up.left.and.arrow.down.right")
                    .font(.system(size: 16))
            }
            .buttonStyle(DSIconButtonStyle(side: 34, radius: DS.radiusPill, active: fill))

            HudDivider()

            // Nhãn nói rõ CÁCH thoát, không chỉ trạng thái: người dùng khoá chuột xong
            // không còn con trỏ để bấm lại nút này.
            Chip(text: tr(model.viewOnly ? "viewOnly" : (model.mouseLocked ? "mouseLocked" : "mouseFree")),
                 active: model.mouseLocked)

            HudDivider()

            Button(tr("end")) { end() }
                .buttonStyle(DSButtonStyle(variant: .danger, size: .sm, pill: true))
        }
        .padding(.bottom, 26)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottom)
    }

    private var connectingOverlay: some View {
        VStack(spacing: 12) {
            Spinner(size: 22)
            MonoText(text: "\(tr("connecting")) \(model.address)", color: DS.textPrimary)
        }
        .padding(26)
        .background(DS.surfacePanel, in: RoundedRectangle(cornerRadius: DS.radiusXl, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: DS.radiusXl, style: .continuous)
                .strokeBorder(DS.borderHairline, lineWidth: DS.hairline)
        )
    }

    private var endedOverlay: some View {
        VStack(alignment: .leading, spacing: 14) {
            Eyebrow(text: trU("sessionEnded"))
            Text(model.endReason)
                .font(DS.ui(DS.textBodyLg))
                .foregroundStyle(DS.textPrimary)
                .fixedSize(horizontal: false, vertical: true)
            Button(tr("back")) { end() }
                .buttonStyle(DSButtonStyle(variant: .primary))
        }
        .frame(maxWidth: 420, alignment: .leading)
        .padding(26)
        .background(DS.surfacePanel, in: RoundedRectangle(cornerRadius: DS.radiusXl, style: .continuous))
        .overlay(
            RoundedRectangle(cornerRadius: DS.radiusXl, style: .continuous)
                .strokeBorder(DS.borderHairline, lineWidth: DS.hairline)
        )
    }

    private func end() {
        model.disconnect()
        route = .connect
    }
}
