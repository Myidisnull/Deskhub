#include "DeskhubBridge.h"

#include "Permissions.h"
#include "input/MacKeyMap.h"

bool dh_map_key(uint16_t mac_key_code, int32_t* out_vk, int32_t* out_scan) {
    if (!out_vk || !out_scan) return false;
    return mackeys::MacToWin(mac_key_code, *out_vk, *out_scan);
}

DHModifier dh_modifier_class(int32_t vk) {
    return DHModifier(int(mackeys::ModifierOf(vk)));
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
