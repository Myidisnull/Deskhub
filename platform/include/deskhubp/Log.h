#pragma once
// =============================================================================
// Log.h — BA macro log, một bản cho cả năm nền tảng.
//
// NHIỆM VỤ
//   LOGI / LOGW / LOGE nhận chuỗi định dạng kiểu printf và đưa dòng log ra kênh
//   chuẩn của từng hệ điều hành. Mọi code C++ dùng chung (platform/, và phần thân
//   của các client) chỉ cần include đúng file này.
//
// ⚠ VÌ SAO GỘP (đổi 31/07/2026)
//   Trước đây có BỐN file Log.h gần giống nhau — client/{macos,linux,ios,android}
//   — cộng thêm bản Windows không có file nào mà rải printf thẳng. Hệ quả không
//   phải là xấu xí mà là SAI THẬT: mỗi bản đều dặn "kiểu có độ rộng thay đổi phải
//   dùng PRIu64 chứ đừng viết %llu", nhưng chỉ bản Ubuntu làm đúng — ba bản kia
//   vẫn còn %llu trong net/SourceQuery.cpp. Một lời dặn chép bốn lần là một lời
//   dặn không ai đọc.
//
// KÊNH RA THEO NỀN (và vì sao mỗi nền một kiểu)
//   Android  logcat. Không có console cho printf chảy vào; xem bằng
//            `adb logcat -s Deskhub`.
//   Windows  printf thẳng. DiagLog::StartProcessLog đã freopen stdout/stderr vào
//            ~/.deskhub/, nên KHÔNG được thêm đường ghi thứ hai — mỗi dòng sẽ ra
//            hai lần.
//   iOS      stderr. ~ nằm trong sandbox nên file ghi ra không ai lấy được; stderr
//            chảy vào console Xcode và Console.app.
//   macOS,   stderr VÀ ~/.deskhub/ (deskhubp::LogEmit). Người dùng bấm đúp vào app
//   Ubuntu   thì stderr không chảy vào đâu cả, nên phải có file — lý do đầy đủ ở
//            LogFile.h.
//
// LƯU Ý KHI DÙNG — ĐỌC TRƯỚC KHI VIẾT MỘT DÒNG LOG CÓ SỐ
//   Kiểu có độ rộng thay đổi giữa 32 và 64 bit (uint64_t, size_t) PHẢI dùng macro
//   của <cinttypes> — `"%" PRIu64` — chứ không viết thẳng %llu. Mọi nền ở đây đều
//   build cho từ hai kiến trúc trở lên (arm64 + x86_64, hoặc arm64 + armeabi-v7a),
//   và %llu cứng sẽ sai trên một trong hai. Trên nhánh POSIX, LogEmit có
//   __attribute__((format(printf))) nên trình dịch bắt giúp; các nhánh khác thì
//   không, nên đây là kỷ luật của người viết.
//
//   Đây là đường LOG, KHÔNG phải đường báo lỗi cho người dùng. Việc gì người dùng
//   cần biết phải đi qua ClientLoop::EndReason/StatusLine hoặc AgentLoop::Status
//   rồi lên UI.
//
// LIÊN QUAN: deskhubp/LogFile.h (đường dẫn file + đệm + thread xả),
//            client/windows/cpp/DiagLog.h (nơi stdout bị đổi hướng),
//            docs/09-diagnostics.md
// =============================================================================

#if defined(__ANDROID__)

#include <android/log.h>

#define DESKHUB_LOG_TAG "Deskhub"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, DESKHUB_LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, DESKHUB_LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, DESKHUB_LOG_TAG, __VA_ARGS__)

#elif defined(_WIN32)

#include <cstdio>

// Một lời gọi printf cho TRỌN dòng, không phải ba lời gọi nối nhau: printf khoá
// FILE* theo từng lời gọi, nên một-lời-gọi-một-dòng là thứ giữ cho log của các
// thread không cài răng lược vào nhau.
#define LOGI(...)                       \
    do {                                \
        std::printf("[Deskhub] ");      \
        std::printf(__VA_ARGS__);       \
        std::printf("\n");              \
    } while (0)
#define LOGW(...) LOGI(__VA_ARGS__)
#define LOGE(...) LOGI(__VA_ARGS__)

#else // Apple + Ubuntu

// TargetConditionals.h CHỈ có trên Apple — Ubuntu không có file này, nên phải hỏi
// __has_include trước. Không có nó thì TARGET_OS_IPHONE không được định nghĩa và
// nhánh dưới rơi đúng vào bản "stderr + file", tức bản Ubuntu cần.
#if __has_include(<TargetConditionals.h>)
#include <TargetConditionals.h>
#endif

#include "deskhubp/LogFile.h"

#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
// iOS: chỉ stderr. Ghi file vô nghĩa trong sandbox (xem PHẠM VI ở LogFile.h).
#include <cstdio>
#define LOGI(...)                          \
    do {                                   \
        std::fprintf(stderr, "[Deskhub] ");\
        std::fprintf(stderr, __VA_ARGS__); \
        std::fprintf(stderr, "\n");        \
    } while (0)
#else
// macOS + Ubuntu: stderr VÀ file. Tiền tố "[Deskhub] " và xuống dòng do LogEmit
// tự thêm, và nó dựng trọn dòng rồi ghi một lần.
#define LOGI(...) deskhubp::LogEmit(__VA_ARGS__)
#endif

#define LOGW(...) LOGI(__VA_ARGS__)
#define LOGE(...) LOGI(__VA_ARGS__)

#endif
