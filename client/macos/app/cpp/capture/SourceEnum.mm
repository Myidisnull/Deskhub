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
// LIÊN QUAN: capture/SourceEnum.h (lý do chọn SCShareableContent)
// =============================================================================
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include "capture/SourceEnum.h"

#include "Log.h"

std::vector<ShareSource> GetShareSources() {
    std::vector<ShareSource> out;

    __block SCShareableContent* content = nil;
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);
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

    int displayIndex = 1;
    for (SCDisplay* d in content.displays) {
        ShareSource s;
        s.displayId = uint32_t(d.displayID);
        // d.width/height của SCDisplay đã là PIXEL, không phải point.
        s.width = uint32_t(d.width);
        s.height = uint32_t(d.height);
        char label[128];
        std::snprintf(label, sizeof(label), "Display %d (%ux%u)", displayIndex++,
            s.width, s.height);
        s.name = label;
        out.push_back(std::move(s));
    }

    LOGI("[Sources] %zu shareable display(s).", out.size());
    return out;
}
