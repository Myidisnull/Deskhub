#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include "deskhubp/media/DisplayEnum.h"

#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include "deskhubp/diag/Log.h"

namespace deskhubp {
namespace {

constexpr int64_t kQueryTimeoutSec = 2;

}

std::vector<deskhub::media::ShareSource> ListDisplays() {
    auto rows = std::make_shared<std::vector<deskhub::media::ShareSource>>();
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                               onScreenWindowsOnly:YES
                                                 completionHandler:^(SCShareableContent* c, NSError* err) {
                                                   if (err)
                                                       LOGE("[Sources] SCShareableContent failed: %s",
                                                           err.localizedDescription.UTF8String);
                                                   int displayIndex = 1;
                                                   for (SCDisplay* d in c.displays) {
                                                       deskhub::media::ShareSource s;
                                                       s.targetId = uint64_t(d.displayID);
                                                       s.width = uint32_t(d.width);
                                                       s.height = uint32_t(d.height);
                                                       char label[128];
                                                       std::snprintf(label, sizeof(label),
                                                           "Display %d (%ux%u)", displayIndex++,
                                                           s.width, s.height);
                                                       s.name = label;
                                                       rows->push_back(std::move(s));
                                                   }
                                                   dispatch_semaphore_signal(sem);
                                                 }];

    const bool timedOut =
        dispatch_semaphore_wait(sem,
            dispatch_time(DISPATCH_TIME_NOW, kQueryTimeoutSec * NSEC_PER_SEC)) != 0;
    if (timedOut) {
        LOGE("[Sources] SCShareableContent timed out (%llds).", kQueryTimeoutSec);
        return {};
    }

    LOGI("[Sources] %zu shareable display(s).", rows->size());
    return std::move(*rows);
}

std::string ListDisplaysError() {
    return {};
}

void ReleaseDisplays() {}

void SetLocalDisplay(uint32_t, uint32_t, const std::string&) {}

}
