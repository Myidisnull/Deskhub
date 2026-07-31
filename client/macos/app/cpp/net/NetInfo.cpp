#include "net/NetInfo.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <algorithm>
#include <cstring>

namespace {

std::string FriendlyName(const char* ifname) {
    const std::string n = ifname ? ifname : "?";
    if (n == "en0") return "Wi-Fi (en0)";
    if (n.rfind("en", 0) == 0) return "Ethernet (" + n + ")";
    if (n.rfind("utun", 0) == 0) return "VPN/Tailscale (" + n + ")";
    if (n.rfind("bridge", 0) == 0) return "Bridge (" + n + ")";
    if (n.rfind("awdl", 0) == 0 || n.rfind("llw", 0) == 0) return "AirDrop (" + n + ")";
    return n;
}

int Rank(const char* ifname) {
    const std::string n = ifname ? ifname : "";
    if (n.rfind("en", 0) == 0) return 0;
    if (n.rfind("utun", 0) == 0) return 1;
    return 2;
}

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

        AdapterAddr a;
        a.name = FriendlyName(ifa->ifa_name);
        a.ip = ip;
        ranked.emplace_back(Rank(ifa->ifa_name), std::move(a));
    }
    freeifaddrs(head);

    std::stable_sort(ranked.begin(), ranked.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });

    out.reserve(ranked.size());
    for (auto& r : ranked) out.push_back(std::move(r.second));
    return out;
}
