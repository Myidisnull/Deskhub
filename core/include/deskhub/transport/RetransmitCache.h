#pragma once
#include "deskhub/protocol/Wire.h"

#include <cstdint>
#include <span>
#include <vector>

namespace deskhub {

class RetransmitCache {
public:
    static constexpr size_t kCacheFrames = 8;

    void Store(std::span<const uint8_t> datagram);

    std::span<const uint8_t> Find(uint32_t frameId, uint16_t pktIndex) const;

    void Reset();

private:
    struct FrameSlot {
        uint32_t frameId = 0;
        bool used = false;
        std::vector<std::vector<uint8_t>> packets;
    };

    FrameSlot* FindSlot(uint32_t frameId);
    const FrameSlot* FindSlot(uint32_t frameId) const;

    FrameSlot slots_[kCacheFrames];
    size_t next_ = 0;
};

}
