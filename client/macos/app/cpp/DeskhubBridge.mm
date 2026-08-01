#include "DeskhubBridge.h"

#include "Permissions.h"
#include "input/MacKeyMap.h"

bool dh_native_key_to_vk(int32_t native_key_code, int32_t* out_vk, int32_t* out_scan) {
    if (!out_vk || !out_scan || native_key_code < 0) return false;
    return mackeys::MacToWin(uint16_t(native_key_code), *out_vk, *out_scan);
}

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
