#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DHPhaseIdle = 0,
    DHPhaseConnecting = 1,
    DHPhaseStreaming = 2,
    DHPhaseEnded = 3,
} DHPhase;

typedef struct {
    uint8_t sourceId;
    uint16_t width;
    uint16_t height;
    char name[256];
} DHSourceInfo;

int dh_list_sources(const char* address, DHSourceInfo* out, int capacity);

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

typedef struct {
    double x;
    double y;
    double width;
    double height;
} DHViewRect;

typedef struct {
    double zoom;
    double panX;
    double panY;
} DHViewTransform;

DHViewRect dh_video_rect(double viewportW, double viewportH, double aspect,
    DHViewTransform t);

DHViewTransform dh_apply_gesture(DHViewTransform cur, double factor, double centroidX,
    double centroidY, double panDeltaX, double panDeltaY, double viewportW, double viewportH,
    double aspect);

bool dh_normalize_pointer(double px, double py, DHViewRect rect, int32_t* nx, int32_t* ny);

#ifdef __cplusplus
}
#endif
