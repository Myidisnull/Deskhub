#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "deskhubp/net/NetInfo.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

#pragma comment(lib, "iphlpapi.lib")

namespace {

std::string ToUtf8(const wchar_t* w) {
    if (!w || !*w) return std::string("?");
    const int len = int(std::wcslen(w));
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string("?");
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, len, s.data(), n, nullptr, nullptr);
    return s;
}

bool IsVirtual(const std::string& friendlyName, const std::string& description) {
    if (friendlyName.rfind("vEthernet", 0) == 0) return true;
    for (const char* marker : {"Hyper-V", "VirtualBox", "VMware", "Virtual Adapter", "Loopback"})
        if (description.find(marker) != std::string::npos) return true;
    return false;
}

uint8_t PrefixLenOf(const IP_ADAPTER_UNICAST_ADDRESS* u) {
    const uint8_t reported = u->OnLinkPrefixLength;
    return reported == 0 || reported > 32 ? uint8_t(24) : reported;
}

}

std::vector<AdapterAddr> ListLocalIPv4() {
    std::vector<AdapterAddr> out;

    ULONG size = 16 * 1024;
    std::vector<uint8_t> buf;
    bool ok = false;
    for (int tries = 0; tries < 3 && !ok; ++tries) {
        buf.resize(size);
        const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                            GAA_FLAG_SKIP_DNS_SERVER;
        const ULONG r = GetAdaptersAddresses(AF_INET, flags, nullptr,
            (IP_ADAPTER_ADDRESSES*)buf.data(), &size);
        if (r == NO_ERROR)
            ok = true;
        else if (r != ERROR_BUFFER_OVERFLOW)
            return out;
    }
    if (!ok) return out;

    for (auto* a = (IP_ADAPTER_ADDRESSES*)buf.data(); a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp) continue;
        if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        const std::string name = ToUtf8(a->FriendlyName);
        const bool isVirtual = IsVirtual(name, ToUtf8(a->Description));
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
            const auto* sin = (const sockaddr_in*)u->Address.lpSockaddr;
            char ip[32];
            if (!InetNtopA(AF_INET, &sin->sin_addr, ip, sizeof(ip))) continue;
            if (std::strncmp(ip, "169.254.", 8) == 0) continue;
            out.push_back(AdapterAddr{name, ip, PrefixLenOf(u), isVirtual});
        }
    }

    std::stable_sort(out.begin(), out.end(), [](const AdapterAddr& x, const AdapterAddr& y) {
        return x.virtualAdapter < y.virtualAdapter;
    });
    return out;
}
