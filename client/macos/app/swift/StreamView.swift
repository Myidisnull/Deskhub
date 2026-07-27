// =============================================================================
// StreamView.swift — màn hình XEM + ĐIỀU KHIỂN.
//
// GIAO DIỆN TRẦN (2026-07-27)
//   Chrome từng dựng trên hệ thiết kế riêng (HUD kính bo pill, chip, chấm sống, biểu
//   đồ RTT). Cả bộ đó đã xoá — giờ chỉ còn `Text`/`Button` dựng sẵn của SwiftUI.
//
// BỐ CỤC: BA HÀNG XẾP DỌC, KHÔNG CHỒNG NHAU
//     [thanh trên]  địa chỉ + dòng số liệu
//     [ô giữa]      video (RemoteView)
//     [thanh dưới]  Lock / Fit / Display / End
//   Hai thanh từng là HUD nổi ĐÈ lên video; tách hẳn ra để không có gì nằm chắn lên
//   nội dung đang xem. Đánh đổi: khung hình mất đúng phần chiều cao của hai thanh.
//
// NỀN ĐEN, KỂ CẢ Ở GIAO DIỆN SÁNG CỦA HỆ
//   Vùng letterbox quanh khung hình phải là màu KHÔNG CÓ, chứ không phải một màu
//   nhạt — nếu không, mắt sẽ đọc nó thành một phần của hình.
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
    @State private var pickerOpen = false

    var body: some View {
        VStack(spacing: 0) {
            statusBar

            ZStack {
                RemoteView(
                    model: model,
                    videoSize: videoSize,
                    fill: fill,
                    mouseLocked: model.mouseLocked,
                    onLayerReady: { layer in DeskhubClient.setLayer(layer) },
                    onLockChanged: { model.mouseLocked = $0 }
                )

                // Lớp phủ trạng thái nằm TRONG ô video, không che hai thanh.
                if !model.endReason.isEmpty {
                    endedOverlay
                } else if model.phase != .streaming {
                    connectingOverlay
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(Color.black)

            bottomBar
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

    // MARK: - Thanh trên

    private var statusBar: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(hostTitle)
                .font(.caption)
                .lineLimit(1)
            if !model.statusLine.isEmpty {
                Text(model.statusLine)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
            }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
    }

    private var hostTitle: String {
        guard model.videoWidth > 0 else { return model.address }
        return "\(model.address) — \(model.videoWidth)×\(model.videoHeight)"
    }

    // MARK: - Thanh dưới

    private var bottomBar: some View {
        HStack(spacing: 10) {
            Button(model.mouseLocked ? "Unlock mouse (F9)" : "Lock mouse (F9)") {
                model.mouseLocked.toggle()
            }

            Button(fill ? "Fit" : "Fill") { fill.toggle() }

            // Chỉ hiện khi có cái để đổi: host một màn hình thì nút này là một câu hỏi
            // không có câu trả lời.
            if model.sources.count > 1 {
                Button("Display") { pickerOpen = true }
                    .popover(isPresented: $pickerOpen, arrowEdge: .bottom) {
                        VStack(alignment: .leading, spacing: 8) {
                            ForEach(model.sources) { source in
                                Button {
                                    model.switchSource(to: source.id)
                                    pickerOpen = false
                                } label: {
                                    HStack(spacing: 8) {
                                        Image(systemName: source.id == model.currentSourceId
                                            ? "largecircle.fill.circle"
                                            : "circle")
                                        Text(sourceLabel(source))
                                        Spacer()
                                    }
                                    .contentShape(Rectangle())
                                }
                                .buttonStyle(.plain)
                            }
                        }
                        .padding(12)
                        .frame(minWidth: 240)
                    }
            }

            Spacer()

            Button("End") { end() }
                .buttonStyle(.borderedProminent)
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
    }

    private func sourceLabel(_ source: Source) -> String {
        let name = source.name.isEmpty ? "Source \(source.id)" : source.name
        return "\(name) — \(source.width)×\(source.height)"
    }

    // MARK: - Lớp phủ

    private var connectingOverlay: some View {
        VStack(spacing: 12) {
            ProgressView()
            Text("Connecting to \(model.address)…")
                .foregroundStyle(.white)
        }
    }

    private var endedOverlay: some View {
        VStack(spacing: 12) {
            Text("Session ended").font(.headline).foregroundStyle(.white)
            Text(model.endReason)
                .foregroundStyle(.white)
                .multilineTextAlignment(.center)
                .fixedSize(horizontal: false, vertical: true)
            Button("Back") { end() }
                .buttonStyle(.borderedProminent)
        }
        .frame(maxWidth: 420)
        .padding(24)
    }

    private func end() {
        model.disconnect()
        route = .connect
    }
}
