#pragma once
#include "deskhub/protocol/Wire.h"

#include <cstdint>
#include <functional>
#include <span>

namespace deskhub {

class InputReceiver {
public:
    using ApplyFn = std::function<void(const InputEvent&)>;

    struct Stats {
        uint64_t packets = 0;
        uint64_t applied = 0;
        uint64_t duplicates = 0;
        uint64_t lost = 0;
    };

    bool HandlePacket(std::span<const uint8_t> payload, const ApplyFn& apply);

    void Reset();

    const Stats& stats() const {
        return stats_;
    }

private:
    int64_t lastAppliedSeq_ = -1;
    Stats stats_{};
};

}
