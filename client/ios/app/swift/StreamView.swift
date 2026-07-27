// =============================================================================
// StreamView.swift — màn hình XEM + ĐIỀU KHIỂN. Dựng theo `DesktopViewer` trong
//                    desktop.jsx; đối ứng client/macos/app/swift/StreamView.swift và
//                    StreamActivity bên Android.
//
// KHÔNG CÒN HEADER VÀ THANH ĐÁY ĐẶC — CHỈ CÒN HUD NỔI
//   Bản cũ có một dải xám đặc trên cùng cho dòng trạng thái và một dải nữa dưới đáy
//   cho nút; hai dải ấy ăn khoảng 15% chiều cao màn hình của đúng thứ duy nhất người
//   ta mở màn này ra để nhìn. Bản thiết kế bỏ hẳn chúng: hình từ máy kia chiếm TRỌN
//   màn hình, mọi thứ khác nổi lên trên dưới dạng HUD bo pill.
//
// NỀN LUÔN ĐEN, KỂ CẢ Ở GIAO DIỆN SÁNG
//   Cả màn này ép colorScheme = .dark: mọi thứ nổi trên video giữ bảng màu tối, đúng
//   như theme-light.css quy định cho `.om-video`. Vùng letterbox phải là màu KHÔNG
//   CÓ, chứ không phải một màu nhạt — nếu không, mắt sẽ đọc nó thành một phần của hình.
//
// THANH PHÍM TẮT LÀ THỨ DESKTOP KHÔNG CÓ
//   macOS/Windows bắt trọn bàn phím thật và gửi thẳng, kể cả Esc, Tab, F-key. Bàn
//   phím ảo của iOS KHÔNG có những phím đó, nên hàng pill cuộn ngang ở đây là đường
//   duy nhất tới chúng. Nó không phải bản sao của một thứ trên desktop — nó là cái
//   giá của việc không có bàn phím thật.
//
// Video sống trong AVSampleBufferDisplayLayer (qua VideoLayerView); SwiftUI chỉ vẽ
// phần chrome, cập nhật mỗi 500ms từ SessionModel.
// =============================================================================
import AVFoundation
import SwiftUI
import UIKit

/// Một phím tắt gửi thẳng sang host — bàn phím ảo không có những phím này.
/// `modVk` != 0 -> tổ hợp (giữ phím bổ trợ rồi gõ phím chính): Ctrl+C, Ctrl+V...
/// Thêm phím mới = thêm một dòng: mã phím ảo Windows + scancode US (bit8 = cờ E0
/// cho phím mở rộng như mũi tên/Del).
private struct Hotkey {
    let label: String
    let vk: Int32
    let scan: Int32
    var modVk: Int32 = 0
    var modScan: Int32 = 0
}

// Không đưa Alt+Tab/phím Win vào: chúng đổi ngữ cảnh trên máy host,
// host sẽ ngừng nhận input (xem TargetHasFocus bên InputInjector).
private let kHotkeys: [Hotkey] = [
    Hotkey(label: "Esc", vk: 0x1B, scan: 0x01),
    Hotkey(label: "Tab", vk: 0x09, scan: 0x0F),
    Hotkey(label: "Enter", vk: 0x0D, scan: 0x1C),
    Hotkey(label: "↑", vk: 0x26, scan: 0x148),
    Hotkey(label: "↓", vk: 0x28, scan: 0x150),
    Hotkey(label: "←", vk: 0x25, scan: 0x14B),
    Hotkey(label: "→", vk: 0x27, scan: 0x14D),
    Hotkey(label: "Del", vk: 0x2E, scan: 0x153),
    Hotkey(label: "Ctrl+C", vk: 0x43, scan: 0x2E, modVk: 0x11, modScan: 0x1D),
    Hotkey(label: "Ctrl+V", vk: 0x56, scan: 0x2F, modVk: 0x11, modScan: 0x1D),
]

struct StreamView: View {
    @Bindable var model: SessionModel
    @Environment(\.scenePhase) private var scenePhase
    @State private var layer: AVSampleBufferDisplayLayer?
    @State private var keyboardOn = false
    @State private var hintVisible = true

    private var streaming: Bool { model.phase == .streaming }

    var body: some View {
        ZStack {
            DS.bgVideo.ignoresSafeArea()

            videoStack

            statusHud
            bottomHud

            if !streaming, model.endReason.isEmpty {
                connectingOverlay
            }
            if !model.endReason.isEmpty {
                endedOverlay
            }
        }
        .environment(\.colorScheme, .dark)
        // Không đẩy/nén layout khi bàn phím ảo mở — bàn phím đè lên video, khung hình
        // đứng yên (người dùng chọn vậy).
        .ignoresSafeArea(.keyboard)
        .onChange(of: scenePhase) { _, newPhase in
            switch newPhase {
            case .background:
                releaseLayer()
            case .active:
                if let layer {
                    DeskhubClient.setLayer(layer)
                }
            case .inactive:
                break
            @unknown default:
                break
            }
        }
        .onAppear {
            UIApplication.shared.isIdleTimerDisabled = true
            model.streamViewAppeared()
        }
        .onDisappear {
            UIApplication.shared.isIdleTimerDisabled = false
            keyboardOn = false
            releaseLayer()
            model.streamViewDisappeared()
        }
        // Cử chỉ trackpad không tự nói ra được, và không có chỗ nào khác để nói: một
        // dòng hướng dẫn nằm mãi trên màn hình thì thành rác ngay từ phiên thứ hai.
        // Hiện 6 giây kể từ lúc có hình rồi tự tắt.
        .task(id: streaming) {
            guard streaming else { return }
            hintVisible = true
            try? await Task.sleep(for: .seconds(6))
            hintVisible = false
        }
        .statusBarHidden()
    }

    // MARK: - Video

    private var videoStack: some View {
        ZStack {
            VideoLayerView { newLayer in
                layer = newLayer
                DeskhubClient.setLayer(newLayer)
            }
            .aspectRatio(aspectRatio, contentMode: .fit)

            // Trackpad phủ CẢ ZStack — gồm vùng đen letterbox quanh video: rê tay ở
            // đâu cũng di được chuột (trackpad chạy theo delta). Con trỏ và toạ độ gửi
            // đi vẫn bám khung video thật (overlay tự tính rect từ videoAspect).
            //
            // "Chỉ xem" thì KHÔNG dựng lớp này: model đã chặn mọi lời gọi input ở cửa
            // xuống C++, nhưng để lại một con trỏ chuột di được mà máy kia không nhúc
            // nhích là nói dối người dùng.
            if streaming, !model.viewOnly {
                TouchInputView(model: model, videoAspect: aspectRatio)
            }

            // View hứng phím: vô hình, chỉ tồn tại để giữ first responder.
            // allowsHitTesting(false): không được nuốt cú chạm của lớp touch.
            KeyInputView(model: model, active: $keyboardOn)
                .frame(width: 1, height: 1)
                .opacity(0)
                .allowsHitTesting(false)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var aspectRatio: CGFloat {
        let width = CGFloat(model.videoWidth)
        let height = CGFloat(model.videoHeight)
        guard width > 0, height > 0 else { return 16.0 / 9.0 }
        return width / height
    }

    // MARK: - HUD trên: máy đang xem + số liệu

    private var statusHud: some View {
        VStack(alignment: .leading, spacing: 8) {
            HudBar {
                StatusDot(live: streaming)
                MonoText(text: hostTitle, size: DS.textMonoSm, color: DS.textPrimary)
                    .lineLimit(1)
                    .truncationMode(.middle)
                StatePill(
                    text: model.viewOnly ? tr("viewOnly") : (streaming ? tr("streaming") : tr("connecting")),
                    tone: streaming ? .live : .neutral
                )
            }

            // Dòng số liệu là HUD RIÊNG, không nhét chung hàng trên: gộp lại thì trên
            // màn dọc nó dài quá bề ngang máy và cả cụm bị co chữ xuống mức không đọc
            // được. Nó cũng chỉ có nghĩa khi đã có hình.
            if streaming, !model.statusLine.isEmpty {
                HudBar {
                    MonoText(text: model.statusLine, color: DS.textPrimary)
                        .lineLimit(1)
                        .minimumScaleFactor(0.75)
                    if model.rttTrace.count >= 2 {
                        HudDivider()
                        Sparkline(values: model.rttTrace)
                            .frame(width: 64, height: 18)
                    }
                }
            }
        }
        .padding(.horizontal, 12)
        .padding(.top, 8)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
        .allowsHitTesting(false)
    }

    private var hostTitle: String {
        guard model.videoWidth > 0 else { return model.address }
        return "\(model.address) — \(model.videoWidth)×\(model.videoHeight)"
    }

    // MARK: - HUD dưới: phím tắt + điều khiển

    private var bottomHud: some View {
        VStack(spacing: 10) {
            if hintVisible, streaming, !model.viewOnly {
                MonoText(text: tr("trackpadHint"), color: DS.textSecondary)
                    .lineLimit(1)
                    .minimumScaleFactor(0.7)
                    .padding(.horizontal, 12)
                    .transition(.opacity)
            }

            // Phím tắt là những pill RỜI chứ không nằm chung một HUD: cả hàng dài hơn
            // bề ngang máy nên phải cuộn được, mà một cái pill kính dài gấp đôi màn
            // hình trượt qua trượt lại thì trông như thanh HUD bị hỏng.
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 8) {
                    ForEach(kHotkeys, id: \.label) { hotkey in
                        Button(hotkey.label) { send(hotkey) }
                            .buttonStyle(DSButtonStyle(variant: .secondary, size: .sm, pill: true))
                    }
                }
                .padding(.horizontal, 12)
            }
            .disabled(!streaming || model.viewOnly)
            .opacity(!streaming || model.viewOnly ? 0.45 : 1)

            HudBar(spacing: 10) {
                Button {
                    keyboardOn.toggle()
                } label: {
                    Image(systemName: "keyboard")
                        .font(.system(size: 16))
                }
                .buttonStyle(DSIconButtonStyle(side: 34, radius: DS.radiusPill, active: keyboardOn))
                .accessibilityLabel(tr("keys"))
                .disabled(!streaming || model.viewOnly)

                HudDivider()

                Chip(text: chipText, active: keyboardOn)

                HudDivider()

                Button(tr("end")) { model.disconnect() }
                    .buttonStyle(DSButtonStyle(variant: .danger, size: .sm, pill: true))
            }
        }
        .padding(.bottom, 10)
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottom)
    }

    private var chipText: String {
        if model.viewOnly { return tr("viewOnly") }
        return keyboardOn ? tr("keysOn") : tr("keys")
    }

    private func send(_ hotkey: Hotkey) {
        if hotkey.modVk != 0 {
            model.keyChord(
                modVk: hotkey.modVk, modScan: hotkey.modScan,
                vk: hotkey.vk, scan: hotkey.scan
            )
        } else {
            model.keyTap(vk: hotkey.vk, scan: hotkey.scan)
        }
    }

    // MARK: - Lớp phủ

    private var connectingOverlay: some View {
        VStack(spacing: 12) {
            Spinner(size: 22)
            MonoText(text: "\(tr("connecting")) \(model.address)", color: DS.textPrimary)
        }
        .padding(22)
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
            Button(tr("back")) { model.disconnect() }
                .buttonStyle(DSButtonStyle(variant: .primary, fullWidth: true))
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

    private func releaseLayer() {
        DeskhubClient.setLayer(nil)
    }
}
