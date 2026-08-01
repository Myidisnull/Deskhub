#pragma once

#include <stdbool.h>
#include <stdint.h>

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

typedef struct {
    uint32_t id;
    uint32_t width;
    uint32_t height;
    char name[256];
} DHShareSource;

typedef struct {
    uint8_t sourceId;
    uint32_t width;
    uint32_t height;
    bool viewerConnected;
    bool zeroCopy;
    double captureFps;
    double sendFps;
    double sendKbps;
    uint32_t rttMs;
    char viewerAddr[64];
    char name[256];
} DHAgentStatus;

typedef struct {
    uint32_t fps;
    uint32_t bitrateMbps;
    uint32_t maxDim;
} DHShareDefaults;

DHShareDefaults dha_default_options(void);

int dha_list_share_sources(DHShareSource* out, int capacity);

bool dha_start(const DHShareSource* sources, int count, uint32_t fps, uint32_t bitrate_mbps,
    uint32_t max_dim);

void dha_stop(void);
bool dha_running(void);

int dha_status(DHAgentStatus* out, int capacity);

const char* dha_local_addresses(void);

#ifdef __cplusplus
}
#endif
