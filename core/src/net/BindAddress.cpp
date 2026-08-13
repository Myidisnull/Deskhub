#include "deskhub/net/BindAddress.h"

#include <algorithm>

#include "deskhub/net/Ipv4.h"

namespace deskhub {

BindChoice SelectBindAddress(const std::string& requested,
    std::span<const std::string> available) {
    if (requested.empty()) return {};
    if (!ParseIPv4(requested)) return {.ip = {}, .fellBack = true};
    const bool present =
        std::find(available.begin(), available.end(), requested) != available.end();
    if (!present) return {.ip = {}, .fellBack = true};
    return {.ip = requested, .fellBack = false};
}

}
