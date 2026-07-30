// =============================================================================
// StreamOverlays.swift — hai lớp phủ nhỏ của màn xem, tách khỏi StreamView.swift.
//
// Cả hai chỉ ĐỌC trạng thái rồi vẽ; không giữ gì của riêng mình, không dính vào bố
// cục safe area hay lớp điều khiển. Để nguyên trong StreamView chỉ làm cái type đó
// dài thêm mà không nói thêm được gì.
//
// LIÊN QUAN: StreamView.swift (nơi dựng cả hai), ViewTransform.swift
// =============================================================================
import SwiftUI

/// "Đang kết nối" / "Phiên đã kết thúc".
///
/// Nằm ở lớp RIÊNG chứ không lồng trong videoArea để được đệm safe area như lớp điều
/// khiển — chữ "Session ended" và nút Back không được chui vào tai thỏ khi máy nằm
/// ngang. Đứng ngoài StreamView vì nó chỉ đọc model, không dính gì tới phần chrome.
struct StatusOverlay: View {
    let model: SessionModel
    let streaming: Bool

    var body: some View {
        if !model.endReason.isEmpty {
            ended
        } else if !streaming {
            connecting
        }
    }

    private var connecting: some View {
        VStack(spacing: 12) {
            ProgressView()
            Text("Connecting to \(model.address)…")
                .foregroundStyle(.white)
        }
    }

    private var ended: some View {
        VStack(spacing: 12) {
            Text("Session ended")
                .font(.headline)
                .foregroundStyle(.white)
            Text(model.endReason)
                .foregroundStyle(.white)
                .multilineTextAlignment(.center)
                .fixedSize(horizontal: false, vertical: true)
            Button("Back") { model.disconnect() }
                .buttonStyle(.borderedProminent)
        }
        .padding(24)
    }
}

/// Hai viên thuốc chỉ dựng khi ĐANG phóng — lúc 1× cả hai đều không nói được gì mà
/// vẫn chiếm chỗ trên khung hình:
///   trên  — một ngón đang làm gì: "Pan" (dời khung) hay "Pointer" (di chuột). Chạm
///           để đổi. Một ngón không thể vừa dời khung vừa di chuột, nên phải có công
///           tắc; nó chỉ xuất hiện khi đang phóng, tức là đúng lúc có gì để dời.
///   dưới  — mức phóng, chạm để về 1×.
struct ZoomControls: View {
    let zoom: CGFloat
    let panMode: Bool
    let onToggleMode: () -> Void
    let onReset: () -> Void

    var body: some View {
        VStack(alignment: .trailing, spacing: 8) {
            pill(panMode ? "Pan" : "Pointer", action: onToggleMode)
                .accessibilityLabel(
                    panMode ? "One finger pans the view" : "One finger moves the pointer"
                )
            pill(String(format: "%.1f×", zoom), action: onReset)
                .accessibilityLabel("Reset zoom")
        }
    }

    private func pill(_ text: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Text(text)
                .font(.caption.weight(.semibold))
                .foregroundStyle(.white)
                .padding(.horizontal, 10)
                .padding(.vertical, 6)
                .background(.black.opacity(0.45), in: Capsule())
                .overlay(Capsule().strokeBorder(.white.opacity(0.25), lineWidth: 1))
        }
        .buttonStyle(.plain)
    }
}
