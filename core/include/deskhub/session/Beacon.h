#pragma once
#include "deskhub/protocol/Wire.h"
#include "deskhub/session/AuthRateLimit.h"

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace deskhub {

class Beacon {
public:
    void SetSources(std::span<const SourceInfo> sources) {
        sources_.assign(sources.begin(), sources.end());
    }

    void SetPasscode(std::string passcode) {
        passcode_ = IsValidPasscode(passcode) ? std::move(passcode) : std::string();
    }

    size_t Reply(std::span<uint8_t> out, std::span<const uint8_t> pkt, uint64_t nowUs = 0,
        uint64_t fromPacked = 0);

private:
    std::vector<SourceInfo> sources_;
    std::string passcode_;
    AuthRateLimit authLimit_;
};

}
