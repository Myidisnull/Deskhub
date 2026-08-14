#pragma once

#include "deskhub/crypto/Keys.h"
#include "deskhub/protocol/Wire.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace deskhub::crypto {

class TrafficCipher {
public:
    void Reset();
    void SetKey(const uint8_t key[kKeySize]);
    bool hasKey() const {
        return hasKey_;
    }

    size_t SealInto(std::span<uint8_t> out, MsgType type, Chan chan, uint32_t sessionId,
        std::span<const uint8_t> plainPayload);
    size_t SealDatagram(std::span<uint8_t> out, std::span<const uint8_t> clearPkt);

    std::optional<std::vector<uint8_t>> OpenPayload(std::span<const uint8_t> pkt,
        uint64_t& recvCounter);
    std::optional<std::vector<uint8_t>> OpenDatagram(std::span<const uint8_t> pkt,
        uint64_t& recvCounter);
    std::optional<std::vector<uint8_t>> OpenPayload(std::span<const uint8_t> pkt);
    std::optional<std::vector<uint8_t>> OpenDatagram(std::span<const uint8_t> pkt);

private:
    uint8_t key_[kKeySize]{};
    bool hasKey_ = false;
    uint64_t sendCounter_ = 0;
    uint64_t recvCounter_ = 0;
    mutable std::mutex mu_;
};

}
