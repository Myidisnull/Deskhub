#pragma once
// =============================================================================
// Discovery.h — quét một lượt tìm host Deskhub trong các mạng máy này đang nối vào.
//
// NHIỆM VỤ
//   Phần platform của tính năng "tìm thấy trong mạng của bạn": mở socket, phát
//   DISCOVER ra broadcast của TỪNG card mạng, thu ANNOUNCE về trong một cửa sổ thời
//   gian ngắn. Phần logic — gộp nhiều câu trả lời của cùng một máy, sắp thứ tự, bỏ
//   câu trả lời của lượt quét cũ — nằm ở core (`deskhub::HostRegistry`), dùng chung
//   với mọi client khác.
//
// VÌ SAO LÀ MỘT HÀM CHẶN, KHÔNG PHẢI MỘT LỚP CHẠY NỀN
//   Cùng khuôn với QuerySources ở các client khác: gọi xong là xong, không có thread
//   nào để dừng, không có vòng đời để rò. Giao diện muốn danh sách cập nhật liên tục
//   thì gọi lại trong một Task nền — mỗi lượt ~1.2 giây, đúng bằng nhịp con quay
//   "đang quét" mà thiết kế vẽ. Đổi lại phải CHẶN, nên:
//
//   ⚠️ PHẢI gọi ngoài UI thread.
//
// CÁI GÌ KHÔNG TÌM ĐƯỢC
//   Broadcast không đi qua router, nên chỉ thấy máy CÙNG mạng nội bộ. Máy qua
//   Tailscale (thường là /32, không có địa chỉ broadcast) vẫn phải gõ địa chỉ tay —
//   đó là giới hạn của thiết kế, không phải thiếu sót của cài đặt.
//
// LIÊN QUAN: deskhub/discovery/HostRegistry.h (gộp + sắp xếp), deskhub::Beacon
//            (đầu kia — host trả lời), net/NetInfo.h (ListLocalBroadcasts),
//            docs/04-protocol.md §3d
// =============================================================================
#include <cstdint>
#include <string>
#include <vector>

// Một máy tìm thấy, đã sẵn sàng để hiển thị.
struct DiscoveredMachine {
    uint32_t hostId = 0;
    std::string name;    // tên máy (UTF-8)
    std::string address; // "ip:port" — đưa thẳng vào ô địa chỉ khi người dùng bấm
    uint32_t rttMs = 0;
    uint8_t sourceCount = 0;
    bool acceptsInput = true;
    bool busy = false;
};

// Quét một lượt. `hostPort` là cổng host nghe (mặc định 47777), `windowMs` là thời
// gian chờ câu trả lời (~1200 là hợp lý: đủ cho Wi-Fi chậm, đủ ngắn để giao diện
// không thấy đơ). Trả danh sách đã gộp và sắp xếp; máy CHẠY HÀM NÀY bị loại ra.
//
// CHẶN tới ~windowMs. Phải gọi ngoài UI thread. Danh sách rỗng = không thấy ai,
// không phải lỗi (mạng có thể chặn broadcast, hoặc đơn giản là không có host nào).
std::vector<DiscoveredMachine> ScanForHosts(uint16_t hostPort, uint32_t windowMs);
