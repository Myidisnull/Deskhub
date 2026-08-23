#pragma once
#include "deskhubp/net/UdpSocket.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace deskhubp {

std::vector<std::optional<uint32_t>> ProbeHostsRttMs(std::span<const NetAddr> hosts,
    uint32_t timeoutMs);
std::optional<uint32_t> ProbeHostRttMs(const NetAddr& host, uint32_t timeoutMs);

}
