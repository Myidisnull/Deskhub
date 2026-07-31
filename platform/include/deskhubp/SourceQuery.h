#pragma once
// =============================================================================
// SourceQuery.h — hỏi host đang chia sẻ những nguồn nào (LIST_SOURCES → SOURCE_LIST).
//                 MỘT bản cho cả năm nền tảng.
//
// NHIỆM VỤ
//   Một lần trao đổi hỏi-đáp duy nhất, chạy TRƯỚC khi có phiên. Kết quả là danh
//   sách nguồn để người dùng chọn xem cái nào.
//
// ⚠ VÌ SAO GỘP (đổi 31/07/2026)
//   Trước đây có năm bản. Sau khi bỏ comment, bản iOS/macOS/Android GIỐNG HỆT nhau,
//   Ubuntu khác 5 dòng và Windows khác 17 — mà toàn bộ khác biệt đó là chuyện định
//   dạng log, không phải giao thức. Tệ hơn: khác biệt của Ubuntu chính là nó dùng
//   PRIu64 đúng như deskhubp/Log.h dặn, còn ba bản kia vẫn viết %llu. Bản Windows
//   thì bỏ hẳn các dòng log — host im lặng suốt 3 giây trên Windows không để lại
//   dấu vết nào. Gộp lại thì cả năm nền cùng đúng.
//
// VỊ TRÍ TRONG LUỒNG NGƯỜI DÙNG
//   <màn kết nối>: gõ địa chỉ → **QuerySources()** → chọn nguồn → <màn xem>
//
// VÌ SAO ĐỨNG NGOÀI ClientLoop
//   Nó chạy trước khi có phiên: mở socket riêng, không sessionId, không thread —
//   gọi xong là xong. Phía host cũng không đưa nó vào HostSession mà để ở Beacon,
//   vì đúng lý do đó (xem deskhub/session/Beacon.h).
//
// KHÔNG DÙNG CHUNG SOCKET VỚI PHIÊN
//   Cổng 0 = hệ thống cấp cổng tạm, host trả lời về chính cổng nguồn đó. Lúc gọi
//   hàm này phiên còn chưa tồn tại, và máy này có thể đang tự chia sẻ trên 47777 —
//   chiếm cổng đó ở đây là tự đá vào host của chính mình.
//
// TÍNH CHẤT CẦN BIẾT TRƯỚC KHI GỌI
//   - CHẶN tới ~3 giây (phát lại LIST_SOURCES vài lần vì UDP có thể mất gói).
//   - PHẢI gọi ngoài UI thread. Chặn UI thread 3 giây là treo app. (Ngoại lệ có
//     chủ ý: MainMenuWindow của Windows chấp nhận điều đó cho một lần bấm Connect,
//     vì hộp thoại chọn nguồn hiện ra ngay sau đó.)
//
// Ý NGHĨA CỦA GIÁ TRỊ TRẢ VỀ
//   false = không mở được socket, hoặc host im lặng suốt 3 giây. Caller hiểu là
//   "host bản cũ / chỉ có một nguồn" và cứ xem nguồn 0 — KHÔNG coi là lỗi tử vong.
//
// LIÊN QUAN: deskhub/protocol/Wire.h (BuildListSources/ParseSourceList),
//            deskhub/session/Beacon.h (đầu kia), deskhubp/UdpSocket.h
// =============================================================================
#include <vector>

#include "deskhubp/UdpSocket.h"

#include "deskhub/protocol/Wire.h"

// CHẶN tới ~3 giây. Trả false nếu không mở được socket hoặc host im lặng — caller
// hiểu là "host bản cũ / chỉ có một nguồn" và cứ xem nguồn 0.
// Phải gọi ngoài UI thread.
bool QuerySources(const NetAddr& server, std::vector<deskhub::SourceInfo>& out);
