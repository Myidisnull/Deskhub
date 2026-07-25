// =============================================================================
// Permissions.mm — cài đặt bằng CGPreflight/CGRequestScreenCaptureAccess và
//                  AXIsProcessTrustedWithOptions.
//
// LIÊN QUAN: agent/Permissions.h (vì sao thiếu quyền lại nguy hiểm)
// =============================================================================
#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>

#include "agent/Permissions.h"

#include "Log.h"

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

bool RequestScreenRecording() {
    // CGRequestScreenCaptureAccess bật hộp thoại ĐÚNG MỘT LẦN trong đời app (hệ
    // thống nhớ đã hỏi rồi). Lần sau nó trả về trạng thái hiện tại mà không hỏi lại
    // — nên UI phải luôn kèm nút mở thẳng System Settings, không dựa vào hộp thoại.
    const bool granted = CGRequestScreenCaptureAccess();
    if (!granted)
        LOGW("[Perm] Screen Recording not granted yet — "
             "enable it in System Settings, then restart Deskhub.");
    return granted;
}

bool HasAccessibility() {
    return AXIsProcessTrusted();
}

bool RequestAccessibility() {
    NSDictionary* opts = @{(__bridge id)kAXTrustedCheckOptionPrompt : @YES};
    const bool granted = AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)opts);
    if (!granted)
        LOGW("[Perm] Accessibility not granted yet — "
             "enable it in System Settings to let the remote side control this Mac.");
    return granted;
}

void OpenScreenRecordingSettings() {
    OpenPane(kScreenRecordingPane);
}

void OpenAccessibilitySettings() {
    OpenPane(kAccessibilityPane);
}

} // namespace macperm
