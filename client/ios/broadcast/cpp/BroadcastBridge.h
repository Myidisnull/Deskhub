#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void dhb_use_app_group(const char* containerPath);

void dhb_push_frame(void* pixelBuffer, uint64_t timestampUs);

void dhb_finish_broadcast(void);

bool dhb_sharing(void);

int dhb_viewer_count(void);

int dhb_last_error(char* out, int capacity);

#ifdef __cplusplus
}
#endif
