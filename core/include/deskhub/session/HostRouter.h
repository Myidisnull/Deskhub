#pragma once
#include "deskhub/control/StreamSize.h"
#include "deskhub/protocol/Wire.h"
#include "deskhub/session/SourcePipelineState.h"

#include <cstdint>
#include <functional>
#include <span>

namespace deskhub {

inline constexpr uint64_t kIdrFlushIdleUs = 200'000;
inline constexpr uint64_t kKeepaliveIdleUs = 500'000;
inline constexpr uint64_t kKeepaliveIntervalUs = 500'000;

SourcePipelineState* RouteDatagram(std::span<SourcePipelineState* const> live,
    const CommonHeader& header, std::span<const uint8_t> pkt);

bool AdoptPeer(SourcePipelineState& st, uint64_t packedAddr);

struct OfferUpdate {
    bool sizeChanged = false;
    bool qualityChanged = false;
    bool sendReconfig = false;
    Reconfig reconfig{};
};

OfferUpdate RefreshOffer(SourcePipelineState& st, uint8_t fallbackFps);

enum class FlushReason : uint8_t { None,
    ForceIdr,
    Keepalive };

FlushReason DueForFlush(const SourcePipelineState& st, uint64_t nowUs);

struct NegotiationHooks {
    std::function<StreamSize(uint16_t clientW, uint16_t clientH)> resolveSize;
};

struct NegotiationResult {
    bool accepted = false;
    StreamSize size;
    int rungCount = 0;
};

NegotiationResult BeginNegotiation(SourcePipelineState& st, const Hello& hello, uint8_t maxFps,
    const NegotiationHooks& hooks);

}
