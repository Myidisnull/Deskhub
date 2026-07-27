// =============================================================================
// LocalInputMonitor.mm — cài đặt bằng NSEvent global monitor.
//
// LIÊN QUAN: input/LocalInputMonitor.h (vì sao cần lọc input mình tự bơm)
// =============================================================================
#import <AppKit/AppKit.h>

#include "input/LocalInputMonitor.h"

#include "Log.h"
#include "deskhubp/Clock.h"

LocalInputMonitor::~LocalInputMonitor() {
    Stop();
}

void LocalInputMonitor::Start() {
    if (monitor_) return;

    // Monitor phải gắn vào run loop CHÍNH; AgentLoop gọi từ thread nền của nó.
    // dispatch_sync chứ không async: Start() trả về là monitor_ phải đã sẵn sàng,
    // nếu không Stop() gọi ngay sau đó sẽ thấy nullptr và bỏ sót việc gỡ.
    dispatch_block_t install = ^{
      if (monitor_) return;
      const NSEventMask mask = NSEventMaskKeyDown | NSEventMaskKeyUp |
                               NSEventMaskFlagsChanged | NSEventMaskMouseMoved |
                               NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown |
                               NSEventMaskOtherMouseDown | NSEventMaskLeftMouseDragged |
                               NSEventMaskRightMouseDragged | NSEventMaskScrollWheel;
      id m = [NSEvent addGlobalMonitorForEventsMatchingMask:mask
                                                    handler:^(NSEvent* e) {
                                                      // Sự kiện do chính ta bơm ra quay lại đây — bỏ qua,
                                                      // nếu không kênh điều khiển tự khoá chính nó
                                                      // (xem cảnh báo ở header).
                                                      CGEventRef cg = e.CGEvent;
                                                      if (cg &&
                                                          CGEventGetIntegerValueField(cg, kCGEventSourceUserData) ==
                                                              LocalInputMonitor::kUserData)
                                                          return;
                                                      lastUs_.store(NowUs(), std::memory_order_relaxed);
                                                    }];
      monitor_ = (__bridge_retained void*)m;
    };
    if ([NSThread isMainThread])
        install();
    else
        dispatch_sync(dispatch_get_main_queue(), install);

    if (!monitor_)
        LOGW("[Input] Could not install local input monitor — "
             "\"host wins\" arbitration is off (needs Accessibility permission).");
}

void LocalInputMonitor::Stop() {
    if (!monitor_) return;
    void* m = monitor_;
    monitor_ = nullptr;
    dispatch_block_t remove = ^{
      [NSEvent removeMonitor:(__bridge_transfer id)m];
    };
    if ([NSThread isMainThread])
        remove();
    else
        dispatch_sync(dispatch_get_main_queue(), remove);
}
