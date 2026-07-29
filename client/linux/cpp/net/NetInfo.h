#pragma once
// =============================================================================
// NetInfo.h — liệt kê địa chỉ IPv4 của máy theo từng card mạng (bản Ubuntu).
//
// NHIỆM VỤ
//   Trả lời câu hỏi "máy này có địa chỉ gì để máy kia gọi tới?". Dùng ở màn hình
//   chính: hiện địa chỉ để người dùng đọc cho máy bên kia, và để AgentLoop in ra
//   khi bắt đầu lắng nghe. Đối ứng client/macos/.../net/NetInfo.h — cùng API, cùng
//   getifaddrs, chỉ khác bảng tên giao diện.
//
// VÌ SAO PHẢI LIỆT KÊ THEO ADAPTER, KHÔNG PHẢI MỘT ĐỊA CHỈ DUY NHẤT
//   Máy Linux hầu như luôn có nhiều IPv4 cùng lúc, và trên Ubuntu desktop số lượng
//   còn nhiều hơn Mac: enp*/wlp* (vật lý), docker0 + br-* (Docker), virbr0
//   (libvirt/KVM), tailscale0. Không có cách nào chắc chắn đoán được cái nào là
//   "đúng" — nó phụ thuộc máy kia nằm ở đâu. Nên hiện HẾT kèm tên giao diện và để
//   người dùng chọn.
//
// KHÁC BẢN macOS Ở HAI ĐIỂM
//   1. TÊN GIAO DIỆN. macOS có en0/utun*; Linux dùng predictable interface names
//      (enp3s0, wlp2s0) từ systemd/udev, cộng thêm loạt bridge ảo. Bảng ánh xạ ở
//      .cpp vì thế khác hẳn.
//   2. XẾP BRIDGE ẢO XUỐNG CUỐI. docker0 (172.17.0.1) và virbr0 (192.168.122.1)
//      luôn Up + Running nên KHÔNG bị bộ lọc nào loại, mà gõ chúng vào máy kia thì
//      chắc chắn không tới. Chúng vẫn được hiện (biết đâu người dùng thật sự stream
//      vào một máy ảo trên chính bridge đó) nhưng đứng CUỐI để không bị nhầm là
//      địa chỉ LAN — đây là cái bẫy phổ biến nhất của bản Ubuntu.
//
// LIÊN QUAN: AgentLoop.cpp (in ra khi mở cổng), gtk/MainWindow.cpp (đẩy lên UI),
//            client/macos/app/cpp/net/NetInfo.h (bản song song)
// =============================================================================
#include <string>
#include <vector>

struct AdapterAddr {
    std::string name; // nhãn dễ đọc ("Ethernet (enp3s0)", "Tailscale (tailscale0)")
    std::string ip;   // "192.168.1.10"
};

// Chỉ trả về giao diện đang Up, bỏ loopback và địa chỉ link-local 169.254.x.x.
std::vector<AdapterAddr> ListLocalIPv4();
