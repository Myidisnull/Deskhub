#include "deskhub/transport/Packetizer.h"

namespace deskhub {

size_t Packetizer::SendFrame(std::span<const uint8_t> nal, uint32_t frameId,
    uint64_t timestampUs, bool idr, const SendFn& send) {
    if (nal.empty() || !send) return 0;
    const size_t count = (nal.size() + kMaxVideoPayload - 1) / kMaxVideoPayload;
    if (count > 0xFFFF) return 0;
    const size_t numGroups = (count + kFecGroupSize - 1) / kFecGroupSize;
    const bool fec = fec_ && numGroups <= kMaxFecGroups;

    VideoHeader vh;
    vh.frameId = frameId;
    vh.timestampUs = timestampUs;
    vh.pktCount = uint16_t(count);

    if (fec) parity_.assign(numGroups * kParityStride, 0);

    for (size_t i = 0; i < count; ++i) {
        const size_t off = i * kMaxVideoPayload;
        const size_t len = (nal.size() - off < kMaxVideoPayload) ? nal.size() - off
                                                                 : kMaxVideoPayload;
        vh.pktIndex = uint16_t(i);
        const bool frameEnd = (i + 1 == count);
        const size_t n = BuildVideoPacket(buf_, sessionId_, vh, idr, frameEnd,
            nal.subspan(off, len));
        if (!n) return 0;
        send(std::span<const uint8_t>(buf_, n));

        if (!fec) continue;
        uint8_t* par = parity_.data() + (i % numGroups) * kParityStride;
        par[0] ^= uint8_t(len >> 8);
        par[1] ^= uint8_t(len & 0xFF);
        for (size_t b = 0; b < len; ++b)
            par[kFecLenPrefix + b] ^= nal[off + b];
    }

    if (!fec) return count;

    FecHeader fh;
    fh.frameId = frameId;
    fh.timestampUs = timestampUs;
    fh.pktCount = uint16_t(count);
    for (size_t g = 0; g < numGroups; ++g) {
        fh.groupIndex = uint8_t(g);
        const size_t n = BuildFecPacket(buf_, sessionId_, fh, idr,
            std::span<const uint8_t>(parity_.data() + g * kParityStride, kParityStride));
        if (!n) return 0;
        send(std::span<const uint8_t>(buf_, n));
    }
    return count;
}

}
