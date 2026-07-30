// =============================================================================
// ViewTransform.swift — mức phóng + đoạn kéo của khung nhìn.
//
// Host 4K co vào màn 6 inch thì chữ nhỏ như hạt vừng, nên màn xem cho phép phóng
// 1×..5×. Toàn bộ TOÁN của việc đó nằm ở đây, một chỗ duy nhất, vì hai nơi dùng nó
// phải khớp nhau ĐẾN TỪNG PIXEL:
//   StreamView          — dựng lớp video đúng bằng khung mà `frame(in:aspect:)` trả về
//                         (layout ở `baseFrame`, phần phóng phủ lên bằng transform).
//   TouchCaptureUIView  — kẹp và chuẩn hoá con trỏ theo đúng khung đó.
// Lệch một pixel là click rơi sai chỗ, mà kiểu sai đó rất khó nhìn ra.
//
// Cử chỉ nào sinh ra (factor, centroid, panDelta) là việc của TouchInputView.swift; ở
// đây chỉ nhận số và quy về trạng thái tuyệt đối.
//
// LIÊN QUAN: TouchInputView.swift (nguồn cử chỉ), StreamView.swift (nơi dựng),
//            StreamOverlays.swift (ZoomControls — công tắc + mức phóng)
// =============================================================================
import SwiftUI

/// Khung nhìn của người xem: mức phóng + đoạn kéo. (1×, .zero) = vừa khít màn hình.
///
/// Đứng riêng một struct vì hai chỗ dùng nó phải khớp nhau ĐẾN TỪNG PIXEL: chỗ đặt
/// lớp video (StreamView) và chỗ kẹp/chuẩn hoá con trỏ (TouchCaptureUIView). Lệch một
/// pixel là click rơi sai chỗ, mà kiểu sai đó rất khó nhìn ra.
struct ViewTransform {
    /// Mức phóng tối đa. 5× đủ đọc chữ nhỏ trên host 4K mà vẫn còn trỏ được: mỗi điểm
    /// trên màn iPhone lúc đó chỉ còn ứng với 1/5 pixel host.
    static let maxZoom: CGFloat = 5

    var zoom: CGFloat = 1
    /// Đoạn kéo tính bằng điểm màn hình. `frame(in:aspect:)` luôn kẹp lại trước khi
    /// dùng, nên phần thừa không bao giờ lọt ra ngoài.
    var pan: CGSize = .zero

    /// Ngưỡng 1.01 chứ không phải 1: chụm hụt một cái để lại 1.0001× thì coi như chưa
    /// phóng, đừng bày huy hiệu ra.
    var isZoomed: Bool { zoom > 1.01 }

    /// Khung ở mức 1× — chỉ letterbox, chưa phóng chưa kéo. StreamView layout lớp video
    /// theo KÍCH THƯỚC của khung này rồi phủ transform lên trên; xem `videoArea`.
    static func baseFrame(in size: CGSize, aspect: CGFloat) -> CGRect {
        ViewTransform().frame(in: size, aspect: aspect)
    }

    /// Khung video HIỂN THỊ bên trong vùng nhìn `size`.
    ///
    /// Không phóng: rect aspect-fit canh giữa, đúng như `.aspectRatio(contentMode: .fit)`
    /// từng làm. Có phóng: nhân kích thước lên `zoom` (vẫn quanh tâm vùng nhìn) rồi
    /// dịch `pan` đã kẹp.
    func frame(in size: CGSize, aspect: CGFloat) -> CGRect {
        guard size.width > 0, size.height > 0 else { return .zero }
        var baseWidth = size.width
        var baseHeight = size.height
        if aspect > 0 {
            baseHeight = size.width / aspect
            if baseHeight > size.height {
                baseHeight = size.height
                baseWidth = size.height * aspect
            }
        }
        let width = baseWidth * zoom
        let height = baseHeight * zoom
        // Kẹp pan: chiều nào khung lớn hơn vùng nhìn thì kéo được tối đa nửa phần thừa
        // (kéo quá là hở nền đen ở rìa); chiều nào nhỏ hơn thì đứng yên giữa.
        let maxX = max(0, (width - size.width) / 2)
        let maxY = max(0, (height - size.height) / 2)
        return CGRect(
            x: (size.width - width) / 2 + min(max(-maxX, pan.width), maxX),
            y: (size.height - height) / 2 + min(max(-maxY, pan.height), maxY),
            width: width, height: height
        )
    }

    /// Nhận một cử chỉ hai ngón và quy về (zoom, pan) tuyệt đối — kiểu xem ảnh.
    ///
    /// Phóng NEO Ở TÂM HAI NGÓN: điểm desktop đang nằm dưới `centroid` phải ở nguyên
    /// chỗ đó, cả khung nở ra quanh nó. Với ánh xạ
    /// `màn hình = tâm + pan + (nội dung - tâm) * zoom`, giữ nguyên điểm dưới tay khi
    /// zoom nhân thêm `ratio` cho ra:
    /// `pan' = (tâm hai ngón - tâm màn hình) * (1 - ratio) + pan * ratio`.
    /// `panDelta` cộng thêm vào sau, để lúc chụm mà tay trượt đi thì ảnh vẫn dính lấy tay.
    mutating func apply(
        factor: CGFloat, centroid: CGPoint, panDelta: CGSize, viewport: CGSize, aspect: CGFloat
    ) {
        guard viewport.width > 0, viewport.height > 0 else { return }
        let newZoom = min(max(1, zoom * factor), Self.maxZoom)
        // Hệ số THẬT sự áp dụng — khác `factor` khi zoom vừa chạm trần/sàn.
        let ratio = newZoom / zoom
        var next = pan
        if ratio != 1 {
            next = CGSize(
                width: (centroid.x - viewport.width / 2) * (1 - ratio) + pan.width * ratio,
                height: (centroid.y - viewport.height / 2) * (1 - ratio) + pan.height * ratio
            )
        }
        next.width += panDelta.width
        next.height += panDelta.height
        zoom = newZoom
        // Lấy lại pan ĐÃ KẸP từ khung mà frame(in:aspect:) dựng ra, thay vì kẹp lần
        // nữa ở đây: chỉ một chỗ giữ luật "không được hở rìa", không sợ hai chỗ lệch.
        pan = next
        let clamped = frame(in: viewport, aspect: aspect)
        pan = CGSize(
            width: clamped.midX - viewport.width / 2,
            height: clamped.midY - viewport.height / 2
        )
    }
}
