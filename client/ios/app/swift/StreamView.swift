// =============================================================================
// StreamView.swift — màn hình XEM + ĐIỀU KHIỂN.
//
// GIAO DIỆN TRẦN (2026-07-27)
//   Chrome từng dựng trên hệ thiết kế riêng (HUD kính bo pill, chip, chấm sống, biểu
//   đồ RTT). Cả bộ đó đã xoá — giờ chỉ còn `Text`/`Button` dựng sẵn của SwiftUI.
//
// BỐ CỤC: VIDEO TRÀN MÀN HÌNH + MỘT BẢNG ĐIỀU KHIỂN THU/MỞ (2026-07-29)
//   Trước đây là ba hàng xếp dọc — [thanh trên: địa chỉ + số liệu] / [video] /
//   [thanh dưới: phím tắt + nút]. Không có gì che video, nhưng khung hình mất VĨNH
//   VIỄN đúng chiều cao hai thanh, kể cả lúc chỉ muốn ngồi xem.
//
//   Giờ video chiếm TRỌN màn hình và toàn bộ chrome dồn vào một lớp phủ góc dưới-phải:
//     thu  → chỉ còn một nút tròn 44pt mờ. Gần như cả màn là khung hình.
//     mở   → bảng phủ đáy: địa chỉ + dòng số liệu, hàng phím tắt, Keyboard/Display/End,
//            và nút ✕ để thu lại.
//   Đánh đổi đảo chiều so với bản trước: lúc MỞ, bảng đè lên phần đáy khung hình —
//   nhưng phần bị đè chỉ tồn tại đúng lúc người dùng chủ động mở nó ra.
//
// SAFE AREA PHẢI ĐỆM BẰNG TAY, VÀ MỖI LỚP MỘT KIỂU
//   Cả ZStack .ignoresSafeArea() nên khung của nó là NGUYÊN màn hình. Insets được đọc
//   bằng GeometryReader đặt NGOÀI phần ignore (bên trong nó luôn trả 0), rồi:
//     lớp điều khiển + lớp phủ → đệm CẢ BỐN phía, không thì nút chui xuống dưới thanh
//                                home và nằm ngang thì bảng thò vào tai thỏ.
//     khung hình             → chỉ đệm HAI BÊN. Nằm ngang, tai thỏ ở cạnh trái/phải
//                                và nó ăn mất pixel thật — rìa desktop biến mất. Còn
//                                trên/dưới chỉ có thanh home vẽ ĐÈ lên, không che mất
//                                gì, nên chỗ đó nhường hết cho khung hình.
//   Insets này gồm cả vùng bàn phím ảo, nên mở bàn phím là bảng tự nhích lên trong khi
//   khung hình đứng yên.
//
// PHÓNG TO KHUNG HÌNH (2026-07-30)
//   Host 4K co vào màn 6 inch thì chữ nhỏ như hạt vừng. Chụm hai ngón để phóng 1×..5×
//   kiểu xem ảnh (điểm giữa hai ngón dính ở đó), rê hai ngón để dời sang khu vực khác.
//   Khung hình KHÔNG bao giờ tự dịch: nó chỉ đổi khi hai ngón bảo nó đổi (xem
//   TouchInputView).
//   `ViewTransform` (ViewTransform.swift) tính khung hiển thị; lớp video được LAYOUT ở
//   khung 1× rồi phủ scaleEffect + offset lên — đổi thẳng frame là đo lại và dựng lại
//   lớp mỗi frame của cử chỉ. Lớp trackpad KHÔNG bị phóng — nó luôn phủ trọn vùng
//   nhìn, và con trỏ lưu toạ độ chuẩn hoá nên phóng/kéo không đụng tới vị trí trên
//   desktop host.
//   Đang phóng thì lớp điều khiển mọc thêm hai viên thuốc (ZoomControls): công tắc
//   Pan/Pointer cho cử chỉ MỘT ngón, và mức phóng — chạm để về 1×.
//
// BẢNG KHÔNG ĐƯỢC LÀM CON TRỎ NHẢY
//   Trackpad (TouchInputView) phủ trọn màn hình, gồm cả phần nằm DƯỚI bảng. Nếu cú
//   chạm vào bảng lọt xuống được lớp đó thì mỗi lần bấm nút con trỏ lại nhảy một
//   phát. Nên StreamView đo khung của lớp điều khiển (toạ độ .global) và đưa xuống
//   TouchInputView; view đó trả false trong point(inside:) cho mọi điểm rơi vào
//   khung — UIKit không giao chạm cho nó ngay từ bước hit-test, chứ không phải chặn
//   muộn ở tầng gesture.
//
// THANH PHÍM TẮT LÀ THỨ DESKTOP KHÔNG CÓ
//   Hàng nút cuộn ngang trong bảng là đường DUY NHẤT tới Esc/Tab/mũi tên — xem
//   Hotkeys.swift.
//
// NHIỀU MÀN HÌNH: ĐỔI NGAY TẠI ĐÂY
//   Host chia sẻ TẤT CẢ màn hình. Nút "Display" (chỉ hiện khi có >1 nguồn) đổi tại
//   chỗ qua SessionModel.switchSource — không phải thoát phiên rồi kết nối lại.
//
// Video sống trong AVSampleBufferDisplayLayer (qua VideoLayerView); SwiftUI chỉ vẽ
// phần chrome, cập nhật mỗi 500ms từ SessionModel.
// =============================================================================
import AVFoundation
import SwiftUI
import UIKit

struct StreamView: View {
    @Bindable var model: SessionModel
    @Environment(\.scenePhase) private var scenePhase
    @State private var layer: AVSampleBufferDisplayLayer?
    @State private var keyboardOn = false
    @State private var pickerOpen = false

    // Bảng điều khiển: mặc định THU. Vào phiên là thấy ngay khung hình trọn vẹn,
    // muốn nút thì mở ra.
    @State private var controlsOpen = false

    // Khung mà lớp điều khiển đang chiếm, toạ độ cửa sổ — trackpad phải mù ở đó.
    @State private var controlsRect: CGRect = .zero

    // Mức phóng + đoạn kéo của khung nhìn — xem ViewTransform.swift.
    @State private var transform = ViewTransform()

    // Một ngón đang làm gì: dời khung (true) hay di chuột (false). Chỉ có nghĩa lúc
    // đang phóng; vào/ra khỏi trạng thái phóng thì tự đặt lại — xem onChange bên dưới.
    @State private var panMode = false

    private var streaming: Bool { model.phase == .streaming }

    var body: some View {
        // GeometryReader nằm NGOÀI mọi .ignoresSafeArea — chỉ ở đó
        // proxy.safeAreaInsets mới trả số thật; đọc nó bên trong một view đã ignore
        // thì luôn là 0. Số này gồm cả vùng bàn phím ảo đang chiếm, nên bảng tự trượt
        // lên khi bàn phím mở.
        GeometryReader { proxy in
            let safeArea = proxy.safeAreaInsets
            ZStack(alignment: .bottomTrailing) {
                // Đệm HAI BÊN đúng bằng safe area: nằm ngang thì tai thỏ nằm ở cạnh
                // trái/phải và nó ăn mất pixel THẬT của khung hình — rìa desktop biến
                // mất. Trên/dưới cố tình KHÔNG đệm: ở đó chỉ có thanh home, một vạch
                // mờ vẽ ĐÈ lên chứ không che mất gì, nên nhường chỗ đó cho khung hình.
                videoArea
                    .padding(.leading, safeArea.leading)
                    .padding(.trailing, safeArea.trailing)
                // maxWidth/Height: ZStack canh .bottomTrailing, không có frame này thì
                // lớp phủ dồn vào góc thay vì đứng giữa màn hình.
                StatusOverlay(model: model, streaming: streaming)
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                    .padding(safeArea)
                // Đệm tay đúng bằng insets thay vì trông cậy vào safe area của
                // SwiftUI: cả ZStack đã ignore (để video tràn viền) nên khung của nó
                // là NGUYÊN màn hình — canh .bottomTrailing mà không đệm thì nút chui
                // xuống dưới thanh home, và nằm ngang thì bảng chạy vào tai thỏ.
                controlsLayer
                    .padding(safeArea)
            }
            .background(Color.black)
            // Tràn viền cho cả stack — gồm cả vùng bàn phím ảo, nên mở bàn phím thì
            // khung hình đứng yên (chỉ bảng nhích lên). Đệm safe area là việc của
            // từng lớp bên trong, mỗi lớp một kiểu.
            .ignoresSafeArea()
        }
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
        // Đổi màn hình = đổi hẳn kích thước desktop; giữ lại mức phóng của nguồn cũ
        // thì người dùng rơi vào một góc ngẫu nhiên của nguồn mới.
        .onChange(of: model.currentSourceId) { _, _ in
            transform = ViewTransform()
        }
        // Phóng lên là để NGẮM, nên vào trạng thái phóng thì một ngón mặc định dời
        // khung; về 1× thì không còn gì để dời, trả lại cho trackpad.
        .onChange(of: transform.isZoomed) { _, zoomed in
            panMode = zoomed
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
        .statusBarHidden()
    }

    private var hostTitle: String {
        guard model.videoWidth > 0 else { return model.address }
        return "\(model.address) — \(model.videoWidth)×\(model.videoHeight)"
    }

    // MARK: - Video toàn màn hình

    private var videoArea: some View {
        // GeometryReader để biết vùng nhìn THẬT của khu video (đã trừ safe area hai
        // bên): ViewTransform cần nó, và lớp trackpad cũng phải phủ đúng bằng nó.
        GeometryReader { proxy in
            let viewport = proxy.size
            let frame = transform.frame(in: viewport, aspect: aspectRatio)
            // Khung 1× — chỉ lấy KÍCH THƯỚC để layout lớp video; chỗ đặt và mức phóng
            // do `frame` quyết, phủ lên bằng transform.
            let base = ViewTransform.baseFrame(in: viewport, aspect: aspectRatio)
            // Canh .topLeading để .offset đặt khung theo đúng toạ độ đã tính; canh
            // giữa thì offset lại cộng thêm vào phần canh, sai gấp đôi.
            ZStack(alignment: .topLeading) {
                VideoLayerView { newLayer in
                    layer = newLayer
                    DeskhubClient.setLayer(newLayer)
                }
                // LAYOUT ở khung 1×, PHÓNG bằng transform — không đặt thẳng frame đã
                // phóng. Hai cách cho ra cùng một hình, nhưng đổi frame là đổi bounds
                // của lớp video, tức là một lượt layout + lớp phải dựng lại nội dung
                // mỗi frame của cử chỉ chụm. scaleEffect thì chỉ là một ma trận:
                // compositor lấy thẳng buffer đã giải mã mà vẽ, không mất nét, không
                // đo lại gì. (Đối ứng scaleX/scaleY của SurfaceView bên Android, nơi
                // cùng vấn đề này gây trễ thấy rõ.)
                //
                // anchor .topLeading: phóng không tự dời gốc view, nên .offset chỉ việc
                // đưa gốc đó tới frame.minX/minY. Theo định nghĩa base * zoom == kích
                // thước frame, nên hai khung trùng khít.
                .frame(width: max(base.width, 1), height: max(base.height, 1))
                .scaleEffect(transform.zoom, anchor: .topLeading)
                .offset(x: frame.minX, y: frame.minY)

                // Trackpad phủ trọn vùng nhìn — gồm vùng đen letterbox: rê tay ở đâu
                // cũng di được chuột (trackpad chạy theo delta). Nó KHÔNG bị phóng
                // theo video; con trỏ bám khung video qua `frame`. `blockedRect`
                // khoét đúng chỗ lớp điều khiển đang đứng.
                if streaming {
                    TouchInputView(
                        model: model,
                        videoRect: frame,
                        blockedRect: controlsRect,
                        panMode: panMode,
                        onTransform: { factor, centroid, panDelta in
                            transform.apply(
                                factor: factor, centroid: centroid, panDelta: panDelta,
                                viewport: viewport, aspect: aspectRatio
                            )
                        }
                    )
                    .frame(width: viewport.width, height: viewport.height)
                }

                // View hứng phím: vô hình, chỉ tồn tại để giữ first responder.
                // allowsHitTesting(false): không được nuốt cú chạm của lớp touch.
                KeyInputView(model: model, active: $keyboardOn)
                    .frame(width: 1, height: 1)
                    .opacity(0)
                    .allowsHitTesting(false)
            }
            .frame(width: viewport.width, height: viewport.height)
            // Phóng to là khung tràn ra ngoài vùng nhìn — cắt đi, không thì nó vẽ
            // lấn vào phần safe area hai bên (chỗ tai thỏ) mà cả bố cục đang cố né.
            .clipped()
        }
    }

    private var aspectRatio: CGFloat {
        let width = CGFloat(model.videoWidth)
        let height = CGFloat(model.videoHeight)
        guard width > 0, height > 0 else { return 16.0 / 9.0 }
        return width / height
    }

    // MARK: - Lớp điều khiển: nút tròn <-> bảng

    private var controlsLayer: some View {
        // Viên thuốc zoom nằm TRONG lớp này chứ không phải một lớp phủ riêng: khung
        // đo bên dưới bao luôn nó, nên trackpad tự động mù ở chỗ đó — không phải
        // thêm vùng chặn thứ hai.
        VStack(alignment: .trailing, spacing: 8) {
            if transform.isZoomed {
                ZoomControls(
                    zoom: transform.zoom,
                    panMode: panMode,
                    onToggleMode: { panMode.toggle() },
                    onReset: {
                        withAnimation(.easeOut(duration: 0.18)) { transform = ViewTransform() }
                    }
                )
            }
            if controlsOpen {
                controlPanel
            } else {
                openButton
            }
        }
        .padding(12)
        // Đo khung THẬT (đã tính cả padding) rồi báo xuống trackpad. Khung co lại
        // ngay khi bảng thu, nên phần trackpad bị khoét cũng nhỏ lại theo.
        .background(
            GeometryReader { proxy in
                Color.clear
                    .onChange(of: proxy.frame(in: .global), initial: true) { _, rect in
                        controlsRect = rect
                    }
            }
        )
    }

    /// Trạng thái THU: một nút tròn mờ, đủ to để bấm trúng mà không chiếm màn hình.
    private var openButton: some View {
        Button {
            withAnimation(.easeOut(duration: 0.18)) { controlsOpen = true }
        } label: {
            Image(systemName: "slider.horizontal.3")
                .font(.system(size: 17, weight: .semibold))
                .foregroundStyle(.white)
                .frame(width: 44, height: 44)
                .background(.black.opacity(0.45), in: Circle())
                .overlay(Circle().strokeBorder(.white.opacity(0.25), lineWidth: 1))
        }
        .buttonStyle(.plain)
        .accessibilityLabel("Show controls")
    }

    /// Trạng thái MỞ: mọi thứ từng nằm ở hai thanh, gộp vào một bảng phủ đáy.
    private var controlPanel: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack(alignment: .top, spacing: 8) {
                VStack(alignment: .leading, spacing: 2) {
                    Text(hostTitle)
                        .font(.caption)
                        .foregroundStyle(.white)
                        .lineLimit(1)
                        .truncationMode(.middle)
                    if streaming, !model.statusLine.isEmpty {
                        Text(model.statusLine)
                            .font(.caption)
                            .foregroundStyle(.white.opacity(0.8))
                            .lineLimit(1)
                            .minimumScaleFactor(0.75)
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)

                Button {
                    withAnimation(.easeOut(duration: 0.18)) { controlsOpen = false }
                } label: {
                    Image(systemName: "xmark")
                        .font(.system(size: 13, weight: .semibold))
                        .foregroundStyle(.white)
                        .frame(width: 32, height: 32)
                        .contentShape(Rectangle())
                }
                .buttonStyle(.plain)
                .accessibilityLabel("Hide controls")
            }

            // Phím tắt cuộn ngang: cả hàng dài hơn bề ngang máy.
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 8) {
                    ForEach(kHotkeys, id: \.label) { hotkey in
                        Button(hotkey.label) { send(hotkey) }
                            .buttonStyle(.bordered)
                    }
                }
                .padding(.vertical, 1) // viền nút không bị ScrollView cắt
            }
            .disabled(!streaming)
            .opacity(streaming ? 1 : 0.45)

            HStack(spacing: 10) {
                Button(keyboardOn ? "Hide keyboard" : "Keyboard") { keyboardOn.toggle() }
                    .buttonStyle(.bordered)
                    .disabled(!streaming)

                // Chỉ hiện khi có cái để đổi: host một màn hình thì nút này là một câu
                // hỏi không có câu trả lời.
                if model.sources.count > 1 {
                    Button("Display") { pickerOpen = true }
                        .buttonStyle(.bordered)
                }

                Spacer()

                Button("End") { model.disconnect() }
                    .buttonStyle(.borderedProminent)
            }
        }
        .padding(12)
        .frame(maxWidth: .infinity)
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 16))
        .confirmationDialog("Display", isPresented: $pickerOpen, titleVisibility: .visible) {
            ForEach(model.sources) { source in
                Button(sourceLabel(source)) { model.switchSource(to: source.id) }
            }
            Button("Cancel", role: .cancel) {}
        }
    }

    private func sourceLabel(_ source: Source) -> String {
        let name = source.name.isEmpty ? "Source \(source.id)" : source.name
        let mark = source.id == model.currentSourceId ? "✓ " : ""
        return "\(mark)\(name) — \(source.width)×\(source.height)"
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

    private func releaseLayer() {
        DeskhubClient.setLayer(nil)
    }
}
