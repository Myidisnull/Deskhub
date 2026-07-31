#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "deskhubp/NetInfo.h"

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

bool IsVirtual(const AdapterAddr& a) {
    return a.name.rfind("vEthernet", 0) == 0;
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
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
            const auto* sin = (const sockaddr_in*)u->Address.lpSockaddr;
            char ip[32];
            if (!InetNtopA(AF_INET, &sin->sin_addr, ip, sizeof(ip))) continue;
            if (std::strncmp(ip, "169.254.", 8) == 0) continue;
            out.push_back(AdapterAddr{ToUtf8(a->FriendlyName), ip});
        }
    }

    std::stable_sort(out.begin(), out.end(), [](const AdapterAddr& x, const AdapterAddr& y) {
        return IsVirtual(x) < IsVirtual(y);
    });
    return out;
}
