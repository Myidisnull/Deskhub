#pragma once
// =============================================================================
// Log.h — ghi log ra stderr, bản Ubuntu. Chép từ client/macos/app/cpp/Log.h.
//
// NHIỆM VỤ
//   App Linux chạy từ terminal (hoặc được desktop khởi động, stderr chảy vào
//   journald của session), nên stderr là đường log rẻ nhất và thấy được ngay bằng
//   `journalctl --user -f` hay chính terminal đã gõ lệnh. Ba macro dưới đây là toàn
//   bộ cơ chế log của phần C++ — dùng chung cho CẢ HAI VAI (client và agent).
//
// VÌ SAO fprintf CHỨ KHÔNG g_log/sd_journal
//   Call site giữ nguyên chuỗi định dạng kiểu printf từ các bản Windows/macOS
//   (LOGI("...%d")). g_log của GLib dùng cùng cú pháp nhưng kéo phần C++ thuần
//   (AgentLoop.cpp, ClientLoop.cpp) phụ thuộc GLib chỉ để in một dòng chữ — mà hai
//   file đó cố ý không biết gì về tầng UI. fprintf giữ được toàn bộ call site không
//   sửa và không thêm phụ thuộc nào.
//
// LƯU Ý KHI DÙNG
//   Kiểu có độ rộng thay đổi (uint64_t, size_t) phải dùng macro <cinttypes> (PRIu64)
//   chứ không viết thẳng %llu — app build cho cả x86_64 lẫn arm64. Đây là đường log,
//   KHÔNG phải đường báo lỗi cho người dùng: việc gì người dùng cần biết phải đi qua
//   ClientLoop::EndReason/StatusLine hoặc AgentLoop::Status rồi lên UI GTK.
//
// LIÊN QUAN: ClientLoop.cpp, AgentLoop.cpp, net/*.cpp (nơi dùng),
//            client/macos/app/cpp/Log.h (bản song song)
// =============================================================================
#include <cstdio>

#define DESKHUB_TAG "[Deskhub] "
#define LOGI(...)                          \
    do {                                   \
        std::fprintf(stderr, DESKHUB_TAG); \
        std::fprintf(stderr, __VA_ARGS__); \
        std::fprintf(stderr, "\n");        \
    } while (0)
#define LOGW(...) LOGI(__VA_ARGS__)
#define LOGE(...) LOGI(__VA_ARGS__)
