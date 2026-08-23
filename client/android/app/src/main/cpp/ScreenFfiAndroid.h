#pragma once
#include <stdint.h>

#include "deskhubp/ffi/ScreenFfi.h"

#ifdef __cplusplus
extern "C" {
#endif

void dh_screen_set_screen_hint(uint32_t width, uint32_t height);

#ifdef __cplusplus
}
#endif
