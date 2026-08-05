#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace deskhub {

inline constexpr size_t kMaxScanTargets = 512;

std::vector<uint32_t> SubnetScanTargets(uint32_t localIp, uint8_t prefixLen,
    size_t maxTargets = kMaxScanTargets);
std::string FormatIPv4(uint32_t ip);
std::string ScanAddressText(uint32_t ip, uint16_t port);

}
