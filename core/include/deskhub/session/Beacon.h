#pragma once
#include "deskhub/protocol/Wire.h"

#include <cstdint>
#include <span>
#include <vector>

namespace deskhub {

class Beacon {
public:
    void SetSources(std::span<const SourceInfo> sources) {
        sources_.assign(sources.begin(), sources.end());
    }

    size_t Reply(std::span<uint8_t> out, std::span<const uint8_t> pkt) const;

private:
    std::vector<SourceInfo> sources_;
};

}
