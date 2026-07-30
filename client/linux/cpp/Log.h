#pragma once
// =============================================================================
// Log.h — ghi log ra stderr VÀ ~/.deskhub/, bản Ubuntu. Chép từ bản macOS.
//
// NHIỆM VỤ
//   Ba macro dưới đây là toàn bộ cơ chế log của phần C++ — dùng chung cho CẢ HAI
//   VAI (client và agent). Mỗi dòng đi ra hai chỗ:
//     - stderr — terminal đã gõ lệnh, hoặc journald nếu app do desktop khởi động
//                (`journalctl --user -f`);
//     - file   — ~/.deskhub/deskhub-<ngày>-<giờ>-<pid>.log (deskhubp/LogFile.h).
//
// VÌ SAO PHẢI CÓ FILE CHỨ KHÔNG CHỈ stderr (đổi 2026-07-30)
//   Khởi động app từ menu ứng dụng thì stderr rơi vào journald — đọc được, nhưng
//   phải biết đúng câu lệnh, và trên vài desktop thì mất hẳn. Ghi thêm ra file thì
//   log luôn tồn tại, và ở cùng đường dẫn với Windows lẫn macOS — xem lý do chọn
//   ~/.deskhub ở deskhubp/LogFile.h.
//
// VÌ SAO fprintf CHỨ KHÔNG g_log/sd_journal
//   Call site giữ nguyên chuỗi định dạng kiểu printf từ các bản Windows/macOS
//   (LOGI("...%d")). g_log của GLib dùng cùng cú pháp nhưng kéo phần C++ thuần
//   (AgentLoop.cpp, ClientLoop.cpp) phụ thuộc GLib chỉ để in một dòng chữ — mà hai
//   file đó cố ý không biết gì về tầng UI.
//
// LƯU Ý KHI DÙNG
//   Kiểu có độ rộng thay đổi (uint64_t, size_t) phải dùng macro <cinttypes> (PRIu64)
//   chứ không viết thẳng %llu — app build cho cả x86_64 lẫn arm64. Đây là đường log,
//   KHÔNG phải đường báo lỗi cho người dùng: việc gì người dùng cần biết phải đi qua
//   ClientLoop::EndReason/StatusLine hoặc AgentLoop::Status rồi lên UI GTK.
//
// LIÊN QUAN: deskhubp/LogFile.h (đường dẫn + đệm + thread xả),
//            ClientLoop.cpp, AgentLoop.cpp, net/*.cpp (nơi dùng),
//            client/macos/app/cpp/Log.h (bản song song)
// =============================================================================
#include "deskhubp/LogFile.h"

// Tiền tố "[Deskhub] " và dấu xuống dòng do LogEmit tự thêm.
#define LOGI(...) deskhubp::LogEmit(__VA_ARGS__)
#define LOGW(...) LOGI(__VA_ARGS__)
#define LOGE(...) LOGI(__VA_ARGS__)
