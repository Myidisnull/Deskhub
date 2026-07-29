#pragma once
// =============================================================================
// SourceQuery.h — hỏi host đang chia sẻ những màn hình nào (LIST_SOURCES →
//                 SOURCE_LIST). Bản Ubuntu, CHÉP từ client/macos/.../net/SourceQuery.h
//                 không sửa dòng nào — cả hai đều POSIX (docs/11 §5).
//
// NHIỆM VỤ
//   Một lần trao đổi hỏi-đáp duy nhất, chạy TRƯỚC khi có phiên. Kết quả là danh
//   sách màn hình để người dùng chọn xem cái nào.
//
// VỊ TRÍ TRONG LUỒNG NGƯỜI DÙNG
//   MainWindow: gõ địa chỉ → **QuerySources()** → chọn nguồn → ViewerWindow
//                                                             → ClientLoop::Start()
//
// VÌ SAO ĐỨNG NGOÀI ClientLoop
//   Nó chạy trước khi có phiên: mở socket riêng, không sessionId, không thread —
//   gọi xong là xong.
//
// TÍNH CHẤT CẦN BIẾT TRƯỚC KHI GỌI
//   - CHẶN tới ~3 giây (phát lại LIST_SOURCES vài lần vì UDP có thể mất gói).
//   - PHẢI gọi ngoài main thread. Chặn main thread 3 giây làm treo UI — trên GTK
//     nghĩa là cửa sổ xám ngoét và compositor báo "ứng dụng không phản hồi". Phía
//     GTK đã bọc trong một std::thread rồi quay về main loop bằng g_idle_add
//     (gtk/MainWindow.cpp).
//
// Ý NGHĨA CỦA GIÁ TRỊ TRẢ VỀ
//   false = không mở được socket, hoặc host im lặng suốt 3 giây. Caller hiểu là
//   "host bản cũ / chỉ có một nguồn" và cứ xem nguồn 0 — KHÔNG coi là lỗi tử vong.
//
// LIÊN QUAN: deskhub/protocol/Wire.h (BuildListSources/ParseSourceList),
//            gtk/MainWindow.cpp (người gọi),
//            client/macos/app/cpp/net/SourceQuery.h (bản song song)
// =============================================================================
#include <vector>

#include "net/UdpSocket.h"

#include "deskhub/protocol/Wire.h"

// CHẶN tới ~3 giây. Trả false nếu không mở được socket hoặc host im lặng — caller
// hiểu là "host bản cũ / chỉ có một nguồn" và cứ xem nguồn 0.
// Phải gọi ngoài main thread.
bool QuerySources(const NetAddr& server, std::vector<deskhub::SourceInfo>& out);
