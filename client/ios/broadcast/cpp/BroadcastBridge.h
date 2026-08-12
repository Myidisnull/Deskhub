#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void dhb_start_broadcast(const char* containerPath, const char* screenName);

void dhb_push_frame(void* pixelBuffer);

void dhb_finish_broadcast(void);

bool dhb_sharing(void);

int dhb_viewer_count(void);

int dhb_last_error(char* out, int capacity);

int dhb_memory_footprint_mb(void);

#ifdef __cplusplus
}
#endif
