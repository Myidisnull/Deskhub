#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "deskhubp/ffi/ClientFfi.h"

#ifdef __cplusplus
extern "C" {
#endif

bool dh_start(const char* address, uint8_t sourceId);

void dh_stop(void);

void dh_set_layer(void* layer);

void dh_key_tap(int32_t vk, int32_t scan);

void dh_key_chord(int32_t mod_vk, int32_t mod_scan, int32_t vk, int32_t scan);

void dh_mouse_move(int32_t nx, int32_t ny);

void dh_mouse_move_rel(int32_t dx, int32_t dy);

void dh_mouse_button(int32_t button, bool down);

void dh_char_tap(uint32_t codepoint);

DHPhase dh_phase(void);

const char* dh_status_line(void);

const char* dh_end_reason(void);

uint32_t dh_video_width(void);
uint32_t dh_video_height(void);

#ifdef __cplusplus
}
#endif
