// =============================================================================
// SourceEnum.mm — cài đặt bằng SCShareableContent.
//
// CHUYỂN API BẤT ĐỒNG BỘ THÀNH ĐỒNG BỘ
//   SCShareableContent chỉ có bản completion-handler. Caller của ta (facade C cho
//   Swift, và AgentLoop) muốn một hàm gọi-xong-là-có-kết-quả, đúng như QuerySources
//   bên vai client. Nên ta chờ bằng dispatch_semaphore với hạn 2 giây: hết hạn thì
//   trả rỗng chứ không treo mãi — API này có thể không bao giờ gọi lại handler khi
//   quyền bị thu hồi giữa chừng.
//
//   Hệ quả bắt buộc: KHÔNG được gọi từ main thread. Handler của SCShareableContent
//   chạy trên một queue nền, nhưng chờ trên main thread vẫn làm treo UI 2 giây, và
//   quan trọng hơn là chặn luôn vòng lặp sự kiện mà các API AppKit cần.
//
// LIÊN QUAN: agent/SourceEnum.h (bộ lọc + lý do chọn SCShareableContent)
// =============================================================================
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include "agent/SourceEnum.h"

#include <algorithm>

#include "Log.h"

namespace {

// Cửa sổ nhỏ hơn cỡ này gần như luôn là lớp phủ, tooltip hay thanh trạng thái nhỏ —
// không phải thứ ai muốn chia sẻ, và cũng dưới ngưỡng encoder nhận (xem kMinEncode*
// trong AgentLoop.cpp).
constexpr uint32_t kMinSize = 120;

// Kích thước PIXEL của một cửa sổ: frame của SCWindow tính bằng ĐIỂM (point), còn
// encoder và giao thức nói bằng PIXEL. Trên màn Retina hai số này chênh nhau 2 lần —
// nhầm chỗ này là stream ra đúng một nửa độ phân giải mà không có lỗi nào báo.
uint32_t PixelsFromPoints(double points, double scale) {
    const double px = points * scale;
    return px > 0 ? uint32_t(px) : 0u;
}

// Thang điểm→pixel của màn hình đang chứa `frame`. NSScreen là đường duy nhất lấy
// được số này mà không phải dựng SCContentFilter cho từng cửa sổ (đắt).
//
// Cửa sổ nằm vắt qua hai màn hình khác backing scale là ca hiếm; chọn màn hình có
// phần giao lớn nhất là ước lượng đúng trong mọi ca thực tế.
double ScaleForFrame(CGRect frame) {
    double bestArea = 0;
    double scale = [NSScreen mainScreen] ? double([NSScreen mainScreen].backingScaleFactor) : 1.0;
    for (NSScreen* s in [NSScreen screens]) {
        const CGRect inter = CGRectIntersection(frame, s.frame);
        if (CGRectIsNull(inter)) continue;
        const double area = inter.size.width * inter.size.height;
        if (area > bestArea) {
            bestArea = area;
            scale = double(s.backingScaleFactor);
        }
    }
    return scale;
}

} // namespace

std::vector<ShareSource> GetShareSources() {
    std::vector<ShareSource> out;

    __block SCShareableContent* content = nil;
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
    // excludingDesktopWindows:YES — bỏ hình nền và cụm icon Desktop, chúng không
    // phải "cửa sổ" theo nghĩa người dùng hiểu.
    // onScreenWindowsOnly:YES — bỏ cửa sổ đã thu nhỏ / ẩn: bắt hình chúng chỉ ra
    // khung đen vì hệ thống không còn vẽ nội dung nữa.
    [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                               onScreenWindowsOnly:YES
                                                 completionHandler:^(SCShareableContent* c, NSError* err) {
                                                   if (err)
                                                       LOGE("[Sources] SCShareableContent failed: %s",
                                                           err.localizedDescription.UTF8String);
                                                   content = c;
                                                   dispatch_semaphore_signal(sem);
                                                 }];
    if (dispatch_semaphore_wait(sem,
            dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC)) != 0) {
        LOGE("[Sources] SCShareableContent timed out (2s).");
        return out;
    }
    if (!content) return out;

    // --- Màn hình trước ---
    int displayIndex = 1;
    for (SCDisplay* d in content.displays) {
        ShareSource s;
        s.target = CaptureTarget::Display(uint32_t(d.displayID));
        // d.width/height của SCDisplay đã là PIXEL, không phải point — khác SCWindow.
        s.width = uint32_t(d.width);
        s.height = uint32_t(d.height);
        char label[128];
        std::snprintf(label, sizeof(label), "Display %d (%ux%u)", displayIndex++,
            s.width, s.height);
        s.name = label;
        out.push_back(std::move(s));
    }

    // --- Rồi tới cửa sổ ---
    const pid_t selfPid = [[NSProcessInfo processInfo] processIdentifier];
    for (SCWindow* w in content.windows) {
        // Bỏ cửa sổ của chính Deskhub: chia sẻ nó cho người đang xem Deskhub là một
        // cái gương hồi quy, và cửa sổ phiên của ta thì không ai cần xem.
        if (w.owningApplication && w.owningApplication.processID == selfPid) continue;
        // Cửa sổ không tiêu đề gần như luôn là lớp phủ vô hình của hệ thống; hiện nó
        // lên danh sách chỉ làm người dùng bối rối vì không biết đó là cái gì.
        if (w.title.length == 0) continue;

        const double useScale = ScaleForFrame(w.frame);

        ShareSource s;
        s.target = CaptureTarget::Window(uint32_t(w.windowID));
        s.width = PixelsFromPoints(w.frame.size.width, useScale);
        s.height = PixelsFromPoints(w.frame.size.height, useScale);
        if (s.width < kMinSize || s.height < kMinSize) continue;

        // Nhãn "Ứng dụng — Tiêu đề": tên app một mình thì không phân biệt nổi 5 cửa
        // sổ Chrome, còn tiêu đề một mình thì không biết nó thuộc app nào.
        NSString* app = w.owningApplication ? w.owningApplication.applicationName : @"";
        NSString* label = app.length ? [NSString stringWithFormat:@"%@ — %@", app, w.title]
                                     : w.title;
        s.name = label.UTF8String ? label.UTF8String : "";
        out.push_back(std::move(s));
    }

    LOGI("[Sources] %zu shareable source(s): %zu display(s), %zu window(s).",
        out.size(), size_t(content.displays.count), out.size() - content.displays.count);
    return out;
}
