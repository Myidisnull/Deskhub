#pragma once

#include "deskhub/ui/Brand.h"

#if defined(__ANDROID__)

#include <android/log.h>

#define DESKHUB_LOG_TAG ::deskhub::brand::kAndroidLogTag
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, DESKHUB_LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, DESKHUB_LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, DESKHUB_LOG_TAG, __VA_ARGS__)

#else

#if __has_include(<TargetConditionals.h>)
#include <TargetConditionals.h>
#endif

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#include <cstdio>
#define LOGI(...)                                                     \
    do {                                                              \
        std::fprintf(stderr, "[%s] ", ::deskhub::brand::kLogLineTag); \
        std::fprintf(stderr, __VA_ARGS__);                            \
        std::fprintf(stderr, "\n");                                   \
    } while (0)
#else
#include "deskhubp/diag/LogFile.h"
#define LOGI(...) deskhubp::LogEmit(__VA_ARGS__)
#endif

#define LOGW(...) LOGI(__VA_ARGS__)
#define LOGE(...) LOGI(__VA_ARGS__)

#endif
