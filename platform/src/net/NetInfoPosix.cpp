#include "deskhubp/net/NetInfo.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>

namespace {

bool StartsWith(const std::string& s, const char* prefix) {
    return s.rfind(prefix, 0) == 0;
}

uint8_t PrefixLenOf(const sockaddr* netmask) {
    if (!netmask || netmask->sa_family != AF_INET) return 24;
    uint32_t mask = ntohl(reinterpret_cast<const sockaddr_in*>(netmask)->sin_addr.s_addr);
    uint8_t bits = 0;
    while (mask & 0x80000000u) {
        ++bits;
        mask <<= 1;
    }
    return bits == 0 ? uint8_t(24) : bits;
}

#if defined(__APPLE__)

std::string FriendlyName(const std::string& n) {
    if (n == "en0") return "Wi-Fi (en0)";
    if (StartsWith(n, "en")) return "Ethernet (" + n + ")";
    if (StartsWith(n, "utun")) return "VPN/Tailscale (" + n + ")";
    if (StartsWith(n, "bridge")) return "Bridge (" + n + ")";
    if (StartsWith(n, "awdl") || StartsWith(n, "llw")) return "AirDrop (" + n + ")";
    return n;
}

int Rank(const std::string& n) {
    if (StartsWith(n, "en")) return 0;
    if (StartsWith(n, "utun")) return 1;
    return 2;
}

#else

std::string FriendlyName(const std::string& n) {
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

int Rank(const std::string& n) {
    if (StartsWith(n, "docker") || StartsWith(n, "br-") || StartsWith(n, "virbr") ||
        StartsWith(n, "vmnet"))
        return 2;
    if (n == "tailscale0" || StartsWith(n, "tun") || StartsWith(n, "tap") || StartsWith(n, "wg"))
        return 1;
    return 0;
}

#endif

}

std::vector<AdapterAddr> ListLocalIPv4() {
    std::vector<AdapterAddr> out;

    ifaddrs* head = nullptr;
    if (getifaddrs(&head) != 0 || !head) return out;

    std::vector<std::pair<int, AdapterAddr>> ranked;
    for (ifaddrs* ifa = head; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (!(ifa->ifa_flags & IFF_UP) || !(ifa->ifa_flags & IFF_RUNNING)) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;

        const auto* sin = reinterpret_cast<const sockaddr_in*>(ifa->ifa_addr);
        char ip[INET_ADDRSTRLEN] = {};
        if (!inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip))) continue;
        if (std::strncmp(ip, "169.254.", 8) == 0) continue;

        const std::string ifname = ifa->ifa_name ? ifa->ifa_name : "?";
        const int rank = Rank(ifname);
        ranked.emplace_back(rank,
            AdapterAddr{FriendlyName(ifname), ip, PrefixLenOf(ifa->ifa_netmask), rank == 2});
    }
    freeifaddrs(head);

    std::stable_sort(ranked.begin(), ranked.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    out.reserve(ranked.size());
    for (auto& r : ranked) out.push_back(std::move(r.second));
    return out;
}
