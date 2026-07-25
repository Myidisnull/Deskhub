// =============================================================================
// NetInfo.cpp — cài đặt bằng getifaddrs(3).
//
// BA BỘ LỌC, MỖI CÁI VÌ MỘT LÝ DO RIÊNG
//   1. Bỏ giao diện không IFF_UP hoặc không IFF_RUNNING — cáp rút ra, Wi-Fi tắt.
//      Địa chỉ vẫn còn trong bảng nhưng gõ vào máy kia thì không tới được.
//   2. Bỏ IFF_LOOPBACK (127.0.0.1) — chỉ gọi được chính máy này, mà đó là ca duy
//      nhất người dùng KHÔNG cần đọc địa chỉ cho ai.
//   3. Bỏ 169.254.x.x (link-local, "self-assigned IP" trong System Settings) — dấu
//      hiệu DHCP hỏng; địa chỉ đó gần như không bao giờ dùng được.
//
// THỨ TỰ TRẢ VỀ CÓ Ý NGHĨA
//   Giao diện vật lý (en*) đứng trước, VPN/tunnel (utun*) sau, còn lại cuối. Người
//   dùng chung LAN — ca phổ biến nhất — thấy ngay dòng đầu là địa chỉ họ cần.
//
// LIÊN QUAN: net/NetInfo.h (lý do thiết kế)
// =============================================================================
#include "net/NetInfo.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <cstring>

namespace {

// Nhãn dễ đọc cho vài tiền tố quen thuộc của BSD. Không khớp cái nào thì giữ
// nguyên tên thiết bị — thà hiện "bridge100" còn hơn giấu mất một đường đi được.
std::string FriendlyName(const char* ifname) {
    const std::string n = ifname ? ifname : "?";
    if (n == "en0") return "Wi-Fi (en0)";
    if (n.rfind("en", 0) == 0) return "Ethernet (" + n + ")";
    if (n.rfind("utun", 0) == 0) return "VPN/Tailscale (" + n + ")";
    if (n.rfind("bridge", 0) == 0) return "Bridge (" + n + ")";
    if (n.rfind("awdl", 0) == 0 || n.rfind("llw", 0) == 0) return "AirDrop (" + n + ")";
    return n;
}

// Hạng ưu tiên khi sắp xếp: nhỏ hơn = hiện trước.
int Rank(const char* ifname) {
    const std::string n = ifname ? ifname : "";
    if (n.rfind("en", 0) == 0) return 0;
    if (n.rfind("utun", 0) == 0) return 1;
    return 2;
}

} // namespace

std::vector<AdapterAddr> ListLocalIPv4() {
    std::vector<AdapterAddr> out;

    ifaddrs* head = nullptr;
    if (getifaddrs(&head) != 0 || !head) return out;

    // Giữ song song hạng ưu tiên để sắp xếp ổn định sau khi quét xong.
    std::vector<std::pair<int, AdapterAddr>> ranked;
    for (ifaddrs* ifa = head; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (!(ifa->ifa_flags & IFF_UP) || !(ifa->ifa_flags & IFF_RUNNING)) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;

        const auto* sin = reinterpret_cast<const sockaddr_in*>(ifa->ifa_addr);
        char ip[INET_ADDRSTRLEN] = {};
        if (!inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip))) continue;
        if (std::strncmp(ip, "169.254.", 8) == 0) continue; // link-local

        AdapterAddr a;
        a.name = FriendlyName(ifa->ifa_name);
        a.ip = ip;
        ranked.emplace_back(Rank(ifa->ifa_name), std::move(a));
    }
    freeifaddrs(head);

    // stable_sort chứ không sort: trong cùng một hạng, giữ đúng thứ tự kernel trả
    // về (en0 trước en1...) thay vì một thứ tự tuỳ hứng của thuật toán.
    std::stable_sort(ranked.begin(), ranked.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    out.reserve(ranked.size());
    for (auto& r : ranked) out.push_back(std::move(r.second));
    return out;
}
