#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

namespace deskhub::media {

enum class RateControl { CBR,
    VBR };

using PacketHandler =
    std::function<void(const uint8_t* data, size_t size, uint64_t timestampUs, bool keyframe)>;

struct EncoderConfig {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t srcWidth = 0;
    uint32_t srcHeight = 0;
    uint32_t fps = 60;
    uint32_t bitrateBps = 20'000'000;
    RateControl rc = RateControl::CBR;
    bool lowLatency = true;
    PacketHandler onPacket;
};

struct DecoderConfig {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps = 60;
};

}
