// =============================================================================
// NetInfo.cpp — cài đặt bằng getifaddrs(3).
//
// BA BỘ LỌC, MỖI CÁI VÌ MỘT LÝ DO RIÊNG (giống hệt bản macOS)
//   1. Bỏ giao diện không IFF_UP hoặc không IFF_RUNNING — cáp rút ra, Wi-Fi tắt.
//      Địa chỉ vẫn còn trong bảng nhưng gõ vào máy kia thì không tới được.
//   2. Bỏ IFF_LOOPBACK (127.0.0.1) — chỉ gọi được chính máy này, mà đó là ca duy
//      nhất người dùng KHÔNG cần đọc địa chỉ cho ai.
//   3. Bỏ 169.254.x.x (link-local) — dấu hiệu DHCP hỏng; địa chỉ đó gần như không
//      bao giờ dùng được.
//
// THỨ TỰ TRẢ VỀ CÓ Ý NGHĨA — VÀ NÓ QUAN TRỌNG HƠN Ở LINUX
//   Vật lý (en*/eth*/wl*) trước, VPN/tunnel (tailscale0, tun*, wg*) sau, BRIDGE ẢO
//   (docker0, br-*, virbr0, vmnet*) CUỐI CÙNG. Lý do ở NetInfo.h: bridge ảo luôn
//   Up+Running nên lọt qua cả ba bộ lọc trên, và 172.17.0.1 của Docker trông y hệt
//   một địa chỉ LAN thật với người không để ý.
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

bool StartsWith(const std::string& s, const char* p) {
    return s.rfind(p, 0) == 0;
}

// Nhãn dễ đọc cho các tiền tố quen thuộc của Linux. Không khớp cái nào thì giữ
// nguyên tên thiết bị — thà hiện "enx00e04c" còn hơn giấu mất một đường đi được.
//
// Tiền tố theo quy ước predictable network interface names của systemd:
//   en* = Ethernet, wl* = WLAN, ww* = WWAN (4G/5G). Tên cũ eth0/wlan0 vẫn gặp trên
//   máy nâng cấp từ bản cũ hoặc trong container, nên nhận cả hai.
std::string FriendlyName(const char* ifname) {
    const std::string n = ifname ? ifname : "?";
    if (StartsWith(n, "en") || StartsWith(n, "eth")) return "Ethernet (" + n + ")";
    if (StartsWith(n, "wl")) return "Wi-Fi (" + n + ")";
    if (StartsWith(n, "ww")) return "Mobile (" + n + ")";
    if (n == "tailscale0") return "Tailscale (" + n + ")";
    if (StartsWith(n, "tun") || StartsWith(n, "tap") || StartsWith(n, "wg"))
        return "VPN (" + n + ")";
    if (StartsWith(n, "docker") || StartsWith(n, "br-")) return "Docker (" + n + ")";
    if (StartsWith(n, "virbr") || StartsWith(n, "vmnet")) return "Virtual machine (" + n + ")";
    return n;
}

// Hạng ưu tiên khi sắp xếp: nhỏ hơn = hiện trước. Xem "thứ tự trả về" ở đầu file.
int Rank(const char* ifname) {
    const std::string n = ifname ? ifname : "";
    if (StartsWith(n, "docker") || StartsWith(n, "br-") || StartsWith(n, "virbr") ||
        StartsWith(n, "vmnet"))
        return 2;
    if (n == "tailscale0" || StartsWith(n, "tun") || StartsWith(n, "tap") || StartsWith(n, "wg"))
        return 1;
    return 0;
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
    // về thay vì một thứ tự tuỳ hứng của thuật toán.
    std::stable_sort(ranked.begin(), ranked.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    out.reserve(ranked.size());
    for (auto& r : ranked) out.push_back(std::move(r.second));
    return out;
}
