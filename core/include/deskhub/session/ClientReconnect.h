#pragma once

#include <cstdint>
#include <string_view>

namespace deskhub {

inline constexpr uint64_t kClientReconnectBackoffUs = 500'000;
inline constexpr uint64_t kClientReconnectBackoffCapUs = 5'000'000;
inline constexpr uint64_t kClientReconnectGraceUs = 60'000'000;

inline bool IsTransientClientDisconnect(std::string_view reason) noexcept {
    return reason == "lost contact with host (timeout)" || reason == "socket error" ||
           reason == "could not connect (timed out)";
}

inline uint64_t ClientReconnectBackoffUs(int attemptIndex) noexcept {
    uint64_t delay = kClientReconnectBackoffUs;
    for (int i = 0; i < attemptIndex; ++i) {
        if (delay >= kClientReconnectBackoffCapUs) return kClientReconnectBackoffCapUs;
        delay *= 2;
    }
    return delay > kClientReconnectBackoffCapUs ? kClientReconnectBackoffCapUs : delay;
}

inline bool ClientReconnectStillWorthTrying(uint64_t sinceLossUs,
    uint64_t graceUs = kClientReconnectGraceUs) noexcept {
    return sinceLossUs < graceUs;
}

}
