#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "deskhubp/ffi/ClientFfi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DHSession DHSession;

DHSession* dh_session_start(const char* address, uint8_t sourceId);

void dh_session_stop(DHSession* s);

void dh_session_set_layer(DHSession* s, void* layer);

void dh_session_key(DHSession* s, int32_t vk, int32_t scan, bool down);

void dh_session_release_all_input(DHSession* s);

void dh_session_mouse_move(DHSession* s, int32_t nx, int32_t ny);

void dh_session_mouse_move_rel(DHSession* s, int32_t dx, int32_t dy);

void dh_session_mouse_button(DHSession* s, int32_t button, bool down);

void dh_session_mouse_wheel(DHSession* s, int32_t delta);

DHPhase dh_session_phase(DHSession* s);
const char* dh_session_status_line(DHSession* s);
const char* dh_session_end_reason(DHSession* s);
uint32_t dh_session_video_width(DHSession* s);
uint32_t dh_session_video_height(DHSession* s);

bool dh_map_key(uint16_t mac_key_code, int32_t* out_vk, int32_t* out_scan);

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
    double captureFps;
    double sendFps;
    double sendKbps;
    uint32_t rttMs;
    char viewerAddr[64];
    char name[256];
} DHAgentStatus;

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
