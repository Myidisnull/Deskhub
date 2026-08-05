#include "deskhub/net/LanScan.h"

#include "deskhub/protocol/Wire.h"

namespace deskhub {

namespace {

constexpr uint8_t kWidestScannablePrefix = 30;

uint32_t HostMask(uint8_t prefixLen) {
    return prefixLen == 0 ? 0xFFFFFFFFu : uint32_t(0xFFFFFFFFull >> prefixLen);
}

uint32_t WindowStart(uint32_t localIp, uint32_t first, uint32_t last, uint32_t window) {
    const uint64_t half = window / 2;
    uint64_t start = uint64_t(localIp) > uint64_t(first) + half ? uint64_t(localIp) - half : first;
    if (start + window - 1 > last) start = uint64_t(last) - window + 1;
    return uint32_t(start);
}

}

std::vector<uint32_t> SubnetScanTargets(uint32_t localIp, uint8_t prefixLen, size_t maxTargets) {
    std::vector<uint32_t> targets;
    if (!localIp || !maxTargets || prefixLen > kWidestScannablePrefix) return targets;

    const uint32_t hostMask = HostMask(prefixLen);
    uint32_t first = (localIp & ~hostMask) + 1;
    uint32_t last = (localIp | hostMask) - 1;
    if (localIp < first || localIp > last) return targets;

    const uint64_t usable = uint64_t(last) - first + 1;
    if (usable > maxTargets) {
        const uint32_t window = uint32_t(maxTargets);
        first = WindowStart(localIp, first, last, window);
        last = first + window - 1;
    }

    targets.reserve(size_t(last - first + 1));
    for (uint32_t ip = first; ip <= last; ++ip)
        if (ip != localIp) targets.push_back(ip);
    return targets;
}

std::string FormatIPv4(uint32_t ip) {
    std::string text = std::to_string((ip >> 24) & 0xFF);
    text += '.';
    text += std::to_string((ip >> 16) & 0xFF);
    text += '.';
    text += std::to_string((ip >> 8) & 0xFF);
    text += '.';
    text += std::to_string(ip & 0xFF);
    return text;
}

std::string ScanAddressText(uint32_t ip, uint16_t port) {
    const std::string text = FormatIPv4(ip);
    if (port == 0 || port == kDeskhubPort) return text;
    return text + ":" + std::to_string(port);
}

}
