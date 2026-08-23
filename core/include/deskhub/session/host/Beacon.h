#pragma once
#include "deskhub/protocol/Wire.h"

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

    void SetCaps(HostCaps caps) {
        caps_ = caps;
    }

    size_t Reply(std::span<uint8_t> out, std::span<const uint8_t> pkt, bool trusted = false) const;

private:
    std::vector<SourceInfo> sources_;
    std::string passcode_;
    HostCaps caps_{};
};

}
