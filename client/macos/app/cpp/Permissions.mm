// =============================================================================
// Permissions.mm — cài đặt bằng CGPreflight/CGRequestScreenCaptureAccess và
//                  AXIsProcessTrustedWithOptions.
//
// LIÊN QUAN: Permissions.h (vì sao thiếu quyền lại nguy hiểm)
// =============================================================================
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>

#include "Permissions.h"

namespace {

// URL đặc biệt của System Settings. Chuỗi này là API công khai (Apple dùng chính nó
// trong tài liệu TCC) nhưng KHÔNG có hằng số nào trong SDK — nên viết tay ở đây, một
// chỗ duy nhất, kèm chú thích để lần sau khỏi phải tra lại.
NSString* const kScreenRecordingPane =
    @"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture";
NSString* const kAccessibilityPane =
    @"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility";

void OpenPane(NSString* url) {
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:url]];
}

} // namespace

namespace macperm {

bool HasScreenRecording() {
    return CGPreflightScreenCaptureAccess();
}

bool HasAccessibility() {
    return AXIsProcessTrusted();
}

void OpenScreenRecordingSettings() {
    OpenPane(kScreenRecordingPane);
}

void OpenAccessibilitySettings() {
    OpenPane(kAccessibilityPane);
}

} // namespace macperm
