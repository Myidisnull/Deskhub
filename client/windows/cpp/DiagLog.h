#pragma once
// =============================================================================
// DiagLog.h — luôn chuyển toàn bộ log của tiến trình ra một file trong ~/.deskhub.
//
// NHIỆM VỤ
//   client.exe là app GUI thuần (không console). Mọi thứ chương trình in ra bằng
//   printf/wprintf trên stdout/stderr — kể cả dòng [DIAG] của docs/09 — được đổi
//   hướng vào một file ngay khi tiến trình khởi động và ở đó tới lúc thoát. Không
//   còn checkbox, không còn console: log LUÔN có sẵn khi cần gửi đi chẩn đoán.
//
// ⚠ THƯ MỤC LOG ĐÃ ĐỔI (2026-07-30)
//   Trước: cạnh exe. Nay: %USERPROFILE%\.deskhub — cùng đường dẫn với bản macOS và
//   Ubuntu, và ghi được cả khi exe nằm trong Program Files (chỗ mà cách cũ luôn
//   thất bại, tức là đúng những lần cài chuẩn nhất lại không có log). Đường dẫn và
//   quy ước đặt tên nằm ở deskhubp/LogFile.h, dùng chung ba nền.
//
// VÌ SAO REDIRECT NGAY LÚC KHỞI ĐỘNG, KHÔNG PHẢI KHI BẮT ĐẦU PHIÊN
//   Sự cố hay xảy ra ở khâu đàm phán/đầu phiên. Bật log muộn (khi người dùng ra
//   lệnh) thì đúng đoạn cần nhất lại chưa được ghi. Mở file một lần ở đầu wWinMain
//   phủ trọn mọi phiên Share/Connect của lần chạy này.
//
// VÌ SAO FREOPEN CHỨ KHÔNG TỰ MỞ MỘT FILE RIÊNG ĐỂ GHI
//   Log rải khắp chương trình bằng printf/wprintf trên stdout. freopen tóm đúng
//   cái stdout ấy — cùng một đường mà `> file` của cmd đi, tức con đường đã biết
//   chắc cho ra UTF-8 đọc được — nên không phải sửa hàng trăm chỗ gọi.
//
// MỖI TIẾN TRÌNH MỘT FILE
//   Tên: deskhub-<ngày>-<giờ>-<pid>.log. Có pid để instance thường và instance
//   admin (Share có điều khiển bung UAC — xem ElevatedShare.h) không ghi đè nhau
//   khi cùng khởi động trong một giây. Vai (agent/client) đã nằm trong từng dòng
//   log nên không cần đưa vào tên file.
//
// LIÊN QUAN: main.cpp (nơi gọi StartProcessLog), docs/09-diagnostics.md
// =============================================================================
#include <string>

// Mở file log trong ~/.deskhub và đổi hướng stdout+stderr vào đó cho tới hết tiến
// trình. Gọi MỘT LẦN ở đầu wWinMain. Trả false nếu không dựng được thư mục hoặc
// không tạo được file — chương trình vẫn chạy tiếp, chỉ là không có log. Khi
// `outPath` khác null, ghi đường dẫn file đã mở vào đó.
bool StartProcessLog(std::wstring* outPath = nullptr);
