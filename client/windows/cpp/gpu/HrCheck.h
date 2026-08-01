#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "deskhubp/diag/Log.h"

#define DH_HR_CHECK_VAL(tag, expr, msg, failValue)                               \
    do {                                                                         \
        const HRESULT dhHr_ = (expr);                                            \
        if (FAILED(dhHr_)) {                                                     \
            LOGE("[%s] %s failed: 0x%08lX", (tag), (msg), (unsigned long)dhHr_); \
            return failValue;                                                    \
        }                                                                        \
    } while (0)

#define DH_HR_CHECK(tag, expr, msg) DH_HR_CHECK_VAL(tag, expr, msg, false)
