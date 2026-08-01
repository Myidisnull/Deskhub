#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "deskhubp/ffi/ClientFfi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DHSession DHSession;

typedef void (*DHStatusCallback)(const char* statusUtf8, void* user);
typedef void (*DHSizeCallback)(uint32_t width, uint32_t height, void* user);
typedef void (*DHClosedCallback)(const char* reasonUtf8, void* user);

typedef struct {
    DHStatusCallback onStatus;
    DHSizeCallback onSize;
    DHClosedCallback onClosed;
    void* user;
} DHSessionCallbacks;

DHSession* dh_session_start(const char* address, uint8_t sourceId, void* surface,
    const DHSessionCallbacks* callbacks);

void dh_session_stop(DHSession* s);

void dh_session_set_layer(DHSession* s, void* layer);

void dh_session_key(DHSession* s, int32_t vk, int32_t scan, bool down);

void dh_session_key_tap(DHSession* s, int32_t vk, int32_t scan);

void dh_session_key_chord(DHSession* s, int32_t modVk, int32_t modScan, int32_t vk, int32_t scan);

void dh_session_hotkey(DHSession* s, int32_t vk, int32_t scan, int32_t modVk, int32_t modScan);

void dh_session_char_tap(DHSession* s, uint32_t codepoint);

void dh_session_release_all_input(DHSession* s);

void dh_session_mouse_move(DHSession* s, int32_t nx, int32_t ny);

void dh_session_mouse_move_rel(DHSession* s, int32_t dx, int32_t dy);

void dh_session_mouse_button(DHSession* s, int32_t button, bool down);

void dh_session_mouse_wheel(DHSession* s, int32_t delta);

void dh_session_mouse_wheel_notches(DHSession* s, int32_t notches);

DHPhase dh_session_phase(DHSession* s);
const char* dh_session_status_line(DHSession* s);
const char* dh_session_end_reason(DHSession* s);
uint32_t dh_session_video_width(DHSession* s);
uint32_t dh_session_video_height(DHSession* s);

#ifdef __cplusplus
}
#endif
