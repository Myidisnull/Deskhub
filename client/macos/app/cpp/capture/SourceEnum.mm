#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include "capture/SourceEnum.h"

#include "deskhubp/Log.h"

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
        s.targetId = uint64_t(d.displayID);
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
