#pragma once
// =============================================================================
// SourceQuery.h — hỏi host đang chia sẻ những cửa sổ nào (LIST_SOURCES → SOURCE_LIST).
//                 Bản Windows, song song với client/macos|ios|android/.../net/SourceQuery.h.
//
// NHIỆM VỤ
//   Một lần trao đổi hỏi-đáp duy nhất, chạy TRƯỚC khi có phiên. Kết quả là danh
//   sách nguồn để người dùng chọn xem cái nào.
//
// VỊ TRÍ TRONG LUỒNG NGƯỜI DÙNG
//   ConnectPage: gõ địa chỉ → **QuerySources()** → PickPage: chọn nguồn
//                                                → ViewerPage → dh_client_start(sourceId)
//
// VÌ SAO ĐỨNG NGOÀI ClientApi/ClientLoop
//   Nó chạy trước khi có phiên: mở socket riêng, không sessionId, không thread —
//   gọi xong là xong. Phía host cũng không đưa nó vào HostSession mà để ở Beacon,
//   vì đúng lý do đó (xem core/include/deskhub/session/Beacon.h).
//
// TÍNH CHẤT CẦN BIẾT TRƯỚC KHI GỌI
//   - CHẶN tới ~3 giây (phát lại LIST_SOURCES vài lần vì UDP có thể mất gói).
//   - PHẢI gọi ngoài UI thread. Chặn UI thread 3 giây là treo app — phía C# bọc
//     trong Task.Run (giống dh_discover_scan).
//
// Ý NGHĨA CỦA GIÁ TRỊ TRẢ VỀ
//   false = không mở được socket, hoặc host im lặng suốt 3 giây. Caller hiểu là
//   "host bản cũ / chỉ có một nguồn" và cứ xem nguồn 0 — KHÔNG coi là lỗi tử vong.
//
// LIÊN QUAN: deskhub/protocol/Wire.h (BuildListSources/ParseSourceList),
//            deskhub/session/Beacon.h (đầu kia), ClientApi.cpp (người gọi)
// =============================================================================
#include <vector>

#include "net/UdpSocket.h"

#include "deskhub/protocol/Wire.h"

// CHẶN tới ~3 giây. Trả false nếu không mở được socket hoặc host im lặng — caller
// hiểu là "host bản cũ / chỉ có một nguồn" và cứ xem nguồn 0.
// Phải gọi ngoài UI thread.
bool QuerySources(const NetAddr& server, std::vector<deskhub::SourceInfo>& out);
