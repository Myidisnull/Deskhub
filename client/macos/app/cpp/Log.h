#pragma once
// =============================================================================
// Log.h — ghi log ra stderr VÀ ~/.deskhub/, bản macOS.
//
// NHIỆM VỤ
//   Ba macro dưới đây là toàn bộ cơ chế log của phần C++ — dùng chung cho CẢ HAI
//   VAI (client và agent). Mỗi dòng đi ra hai chỗ:
//     - stderr — console của Xcode, và Terminal khi chạy app từ dòng lệnh;
//     - file   — ~/.deskhub/deskhub-<ngày>-<giờ>-<pid>.log (deskhubp/LogFile.h).
//
// VÌ SAO PHẢI CÓ FILE CHỨ KHÔNG CHỈ stderr (đổi 2026-07-30)
//   Người dùng bấm đúp vào Deskhub.app thì stderr không chảy vào đâu cả: muốn đọc
//   log họ phải biết chạy Contents/MacOS/app từ Terminal. Nghĩa là đúng những báo
//   cáo lỗi từ người không phải lập trình viên lại là những báo cáo KHÔNG có log.
//   Ghi thêm ra file thì log luôn tồn tại, và ở cùng đường dẫn với Windows lẫn
//   Ubuntu — xem lý do chọn ~/.deskhub ở deskhubp/LogFile.h.
//
// VÌ SAO fprintf CHỨ KHÔNG os_log
//   Call site giữ nguyên chuỗi định dạng kiểu printf từ bản Android (LOGI("...%d")).
//   os_log dùng chuỗi định dạng riêng, không tương thích trực tiếp — giữ printf thì
//   toàn bộ call site không phải sửa, đổi lại mất phần lọc theo subsystem.
//
// LƯU Ý KHI DÙNG
//   Kiểu có độ rộng thay đổi (uint64_t, size_t) phải dùng macro <cinttypes> (PRIu64)
//   chứ không viết thẳng %llu — app build cho cả arm64 (Apple Silicon) lẫn x86_64
//   (Intel). Đây là đường log, KHÔNG phải đường báo lỗi cho người dùng: việc gì
//   người dùng cần biết phải đi qua ClientLoop::EndReason/StatusLine hoặc
//   AgentLoop::StatusLine rồi lên UI.
//
// LIÊN QUAN: deskhubp/LogFile.h (đường dẫn + đệm + thread xả),
//            client/ClientLoop.cpp, agent/AgentLoop.cpp, net/*.cpp (nơi dùng),
//            client/ios/app/cpp/Log.h (bản song song — iOS ở trong sandbox nên
//            vẫn chỉ stderr)
// =============================================================================
#include "deskhubp/LogFile.h"

// Tiền tố "[Deskhub] " và dấu xuống dòng do LogEmit tự thêm.
#define LOGI(...) deskhubp::LogEmit(__VA_ARGS__)
#define LOGW(...) LOGI(__VA_ARGS__)
#define LOGE(...) LOGI(__VA_ARGS__)
