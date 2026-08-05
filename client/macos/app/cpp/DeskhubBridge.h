#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "deskhubp/ffi/AgentSession.h"
#include "deskhubp/ffi/ClientFfi.h"
#include "deskhubp/ffi/ClientSession.h"
#include "deskhubp/ffi/DiscoveryFfi.h"

#ifdef __cplusplus
extern "C" {
#endif

bool dh_has_screen_recording(void);
void dh_open_screen_recording_settings(void);
bool dh_has_accessibility(void);
void dh_open_accessibility_settings(void);

#ifdef __cplusplus
}
#endif
