#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#import <CoreGraphics/CoreGraphics.h>

#include "Permissions.h"

namespace {

NSString* const kScreenRecordingPane =
    @"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture";
NSString* const kAccessibilityPane =
    @"x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility";

void OpenPane(NSString* url) {
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:url]];
}

}

namespace macperm {

bool HasScreenRecording() {
    return CGPreflightScreenCaptureAccess();
}

bool RequestScreenRecording() {
    return CGRequestScreenCaptureAccess();
}

bool HasAccessibility() {
    return AXIsProcessTrusted();
}

bool RequestAccessibility() {
    NSDictionary* options = @{(__bridge NSString*)kAXTrustedCheckOptionPrompt: @YES};
    return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
}

void OpenScreenRecordingSettings() {
    OpenPane(kScreenRecordingPane);
}

void OpenAccessibilitySettings() {
    OpenPane(kAccessibilityPane);
}

}
