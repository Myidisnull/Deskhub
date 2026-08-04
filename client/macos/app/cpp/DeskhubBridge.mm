#include "DeskhubBridge.h"

#include "Permissions.h"

bool dh_has_screen_recording(void) {
    return macperm::HasScreenRecording();
}

void dh_open_screen_recording_settings(void) {
    macperm::OpenScreenRecordingSettings();
}

bool dh_has_accessibility(void) {
    return macperm::HasAccessibility();
}

void dh_open_accessibility_settings(void) {
    macperm::OpenAccessibilitySettings();
}
