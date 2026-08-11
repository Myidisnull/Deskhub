#include "deskhub/protocol/Wire.h"
#include "deskhub/transport/Reassembler.h"

#include <cstddef>
#include <cstdint>
#include <span>

using namespace deskhub;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    Reassembler ra(16'667);
    const std::span<const uint8_t> all(data, size);
    uint64_t now = 1'000'000;
    size_t at = 0;

    while (at + 2 <= size) {
        const size_t want = (size_t(data[at]) << 8) | data[at + 1];
        at += 2;
        const size_t take = want < size - at ? want : size - at;
        const std::span<const uint8_t> d = all.subspan(at, take);
        at += take;

        const auto h = ParseCommonHeader(d);
        if (h) {
            if (h->type == MsgType::FecPacket) {
                if (const auto v = ParseFecPacket(*h, PayloadOf(d))) ra.PushFec(*v, now);
            } else if (const auto v = ParseVideoPacket(*h, PayloadOf(d))) {
                ra.Push(*v, now);
            }
        }
        while (ra.PopReady(now)) {
        }
        uint32_t frameId = 0;
        uint16_t indices[16];
        ra.PlanNack(now, 20'000, frameId, indices);
        ra.TakeLossEvent();
        now += 1'000;
    }
    return 0;
}
