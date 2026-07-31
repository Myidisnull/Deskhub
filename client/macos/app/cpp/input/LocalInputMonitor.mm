#import <AppKit/AppKit.h>

#include "input/LocalInputMonitor.h"

#include "deskhubp/Log.h"
#include "deskhubp/Clock.h"

LocalInputMonitor::~LocalInputMonitor() {
    Stop();
}

void LocalInputMonitor::Start() {
    if (monitor_) return;

    dispatch_block_t install = ^{
      if (monitor_) return;
      const NSEventMask mask = NSEventMaskKeyDown | NSEventMaskKeyUp |
                               NSEventMaskFlagsChanged | NSEventMaskMouseMoved |
                               NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown |
                               NSEventMaskOtherMouseDown | NSEventMaskLeftMouseDragged |
                               NSEventMaskRightMouseDragged | NSEventMaskScrollWheel;
      id m = [NSEvent addGlobalMonitorForEventsMatchingMask:mask
                                                    handler:^(NSEvent* e) {
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
