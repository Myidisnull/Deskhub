#pragma once
#include "deskhub/protocol/Wire.h"

#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace deskhub {

class Packetizer {
public:
    using SendFn = std::function<void(std::span<const uint8_t>)>;

    void SetSessionId(uint32_t id) {
        sessionId_ = id;
    }
    uint32_t sessionId() const {
        return sessionId_;
    }

    void SetFecEnabled(bool on) {
        fec_ = on;
    }
    bool fecEnabled() const {
        return fec_;
    }

    size_t SendFrame(std::span<const uint8_t> nal, uint32_t frameId, uint64_t timestampUs,
        bool idr, const SendFn& send);

    static constexpr size_t kParityStride = kFecLenPrefix + kMaxVideoPayload;

private:
    uint32_t sessionId_ = 0;
    bool fec_ = false;
    uint8_t buf_[kMaxDatagram] = {};
    std::vector<uint8_t> parity_;
};

}
