#pragma once
// =============================================================================
// NetInfo.h — liệt kê địa chỉ IPv4 của máy theo từng card mạng (bản macOS).
//
// NHIỆM VỤ
//   Trả lời câu hỏi "máy này có địa chỉ gì để máy kia gọi tới?". Dùng ở màn hình
//   chia sẻ: hiện địa chỉ để người dùng đọc cho máy bên kia, và để AgentLoop in ra
//   khi bắt đầu lắng nghe. Đối ứng client/windows/net/NetInfo.h — cùng API, khác
//   mỗi cách hỏi hệ điều hành (getifaddrs thay GetAdaptersAddresses).
//
// VÌ SAO PHẢI LIỆT KÊ THEO ADAPTER, KHÔNG PHẢI MỘT ĐỊA CHỈ DUY NHẤT
//   Mac hầu như luôn có nhiều IPv4 cùng lúc: Wi-Fi (en0), Ethernet/Thunderbolt
//   (en1..), bridge ảo của Docker/UTM, và utun* của Tailscale/VPN. Không có cách
//   nào chắc chắn đoán được cái nào là "đúng" — nó phụ thuộc máy kia nằm ở đâu:
//   cùng LAN thì là en0, qua Internet thì là utun của Tailscale. Nên hiện HẾT kèm
//   tên giao diện và để người dùng chọn.
//
// KHÁC BẢN WINDOWS Ở MỘT ĐIỂM: TÊN GIAO DIỆN
//   Windows có "tên thân thiện" ("Wi-Fi", "Ethernet"). BSD chỉ có tên thiết bị
//   (en0, utun3). Ta ánh xạ vài tiền tố quen thuộc sang nhãn dễ đọc (xem .cpp) và
//   để nguyên phần còn lại — thà hiện "utun3" còn hơn giấu mất đường Tailscale.
//
// LIÊN QUAN: agent/AgentLoop.cpp (in ra khi mở cổng), DeskhubBridge.mm
//            (đẩy lên UI), client/windows/net/NetInfo.h (bản song song)
// =============================================================================
#include <string>
#include <vector>

struct AdapterAddr {
    std::string name; // nhãn dễ đọc ("Wi-Fi (en0)", "Tailscale (utun3)")
    std::string ip;   // "192.168.1.10"
};

// Chỉ trả về giao diện đang Up, bỏ loopback và địa chỉ link-local 169.254.x.x.
std::vector<AdapterAddr> ListLocalIPv4();
