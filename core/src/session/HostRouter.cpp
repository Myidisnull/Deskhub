#include "deskhub/session/HostRouter.h"

namespace deskhub {

SourcePipelineState* RouteDatagram(std::span<SourcePipelineState* const> live,
    const CommonHeader& header, std::span<const uint8_t> pkt) {
    if (header.type == MsgType::Hello) {
        const auto hello = ParseHello(PayloadOf(pkt));
        if (!hello) return nullptr;
        for (SourcePipelineState* p : live)
            if (p && p->sourceId == hello->sourceId) return p;
        return nullptr;
    }

    if (!header.sessionId) return nullptr;
    for (SourcePipelineState* p : live)
        if (p && p->session && p->session->sessionId() == header.sessionId) return p;
    return nullptr;
}

bool AdoptPeer(SourcePipelineState& st, uint64_t packedAddr) {
    if (st.peerPacked.load(std::memory_order_relaxed) == packedAddr) return false;
    st.peerPacked.store(packedAddr, std::memory_order_release);
    return true;
}

OfferUpdate RefreshOffer(SourcePipelineState& st, uint8_t fallbackFps) {
    OfferUpdate out;
    out.sizeChanged = st.sizeChanged.exchange(false, std::memory_order_acq_rel);
    out.qualityChanged = st.qualityChanged.exchange(false, std::memory_order_acq_rel);

    if (st.paused.load(std::memory_order_acquire)) return out;
    if (!out.sizeChanged && !out.qualityChanged) return out;

    st.offer.width = uint16_t(st.srcW.load());
    st.offer.height = uint16_t(st.srcH.load());
    st.offer.fps = uint8_t(st.step.fps ? st.step.fps : fallbackFps);
    st.offer.bitrateBps = st.curBitrateBps.load(std::memory_order_relaxed);
    if (st.session) st.session->SetOffer(st.offer);

    if (!st.peerPacked.load(std::memory_order_acquire)) return out;
    if (!st.session || st.session->state() != HostSession::State::Streaming) return out;

    out.sendReconfig = true;
    out.reconfig = Reconfig{st.offer.width, st.offer.height, st.offer.bitrateBps, st.offer.fps};
    return out;
}

FlushReason DueForFlush(const SourcePipelineState& st, uint64_t nowUs) {
    if (!st.session || st.session->state() != HostSession::State::Streaming)
        return FlushReason::None;

    const uint64_t sinceFrameUs = nowUs - st.lastFrameUs.load(std::memory_order_relaxed);
    if (st.forceIdr.load() && sinceFrameUs > kIdrFlushIdleUs) return FlushReason::ForceIdr;
    if (sinceFrameUs > kKeepaliveIdleUs && nowUs - st.lastKeepaliveUs >= kKeepaliveIntervalUs)
        return FlushReason::Keepalive;
    return FlushReason::None;
}

NegotiationResult BeginNegotiation(SourcePipelineState& st, const Hello& hello, uint8_t maxFps,
    const NegotiationHooks& hooks) {
    st.step = QualityStep{};

    NegotiationResult out;
    if (!hooks.resolveSize) return out;

    const StreamSize t = hooks.resolveSize(hello.maxWidth, hello.maxHeight);
    if (!t.width || !t.height) return out;

    st.ladder = std::make_unique<QualityLadder>(uint16_t(t.width), uint16_t(t.height), maxFps);
    st.step = st.ladder->current();
    st.curFps.store(st.step.fps, std::memory_order_relaxed);

    st.offer.width = uint16_t(t.width);
    st.offer.height = uint16_t(t.height);
    st.offer.fps = uint8_t(st.step.fps);
    st.offer.bitrateBps = st.curBitrateBps.load(std::memory_order_relaxed);
    if (st.session) st.session->SetOffer(st.offer);

    out.accepted = true;
    out.size = t;
    out.rungCount = st.ladder->rungCount();
    return out;
}

}
