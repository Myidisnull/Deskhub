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

bool HasAccessibility() {
    return AXIsProcessTrusted();
}

void OpenScreenRecordingSettings() {
    OpenPane(kScreenRecordingPane);
}

void OpenAccessibilitySettings() {
    OpenPane(kAccessibilityPane);
}

}
