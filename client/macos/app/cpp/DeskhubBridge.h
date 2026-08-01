#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "deskhubp/ffi/AgentSession.h"
#include "deskhubp/ffi/ClientFfi.h"
#include "deskhubp/ffi/ClientSession.h"

#ifdef __cplusplus
extern "C" {
#endif

bool dh_map_key(uint16_t mac_key_code, int32_t* out_vk, int32_t* out_scan);

typedef enum {
    DHModifierNone = 0,
    DHModifierShift = 1,
    DHModifierControl = 2,
    DHModifierOption = 3,
    DHModifierCommand = 4,
    DHModifierCapsLock = 5,
} DHModifier;

DHModifier dh_modifier_class(int32_t vk);

bool dh_has_screen_recording(void);
void dh_open_screen_recording_settings(void);
bool dh_has_accessibility(void);
void dh_open_accessibility_settings(void);

#ifdef __cplusplus
}
#endif
