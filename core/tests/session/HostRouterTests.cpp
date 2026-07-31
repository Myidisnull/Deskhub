#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/session/HostRouter.h"

#include <cstdio>
#include <memory>
#include <vector>

using namespace deskhub;

namespace {

constexpr uint32_t kStartBps = 20'000'000;
constexpr uint32_t kMinBps = 1'000'000;
constexpr uint64_t kT0 = 100'000'000;

std::unique_ptr<SourcePipelineState> MakePipe(uint8_t sourceId) {
    auto p = std::make_unique<SourcePipelineState>(kStartBps, kMinBps);
    p->sourceId = sourceId;
    return p;
}

void GiveSession(SourcePipelineState& st) {
    HostCallbacks cb;
    cb.send = [](std::span<const uint8_t>) {};
    cb.randomBytes = TestRandomBytes;
    st.session = std::make_unique<HostSession>(cb, StreamParams{1920, 1080, 60, kStartBps});
}

Datagram BuildHelloFor(uint8_t sourceId) {
    uint8_t buf[kMaxDatagram];
    Hello h{};
    h.clientId = 0x1234;
    h.codecMask = kCodecMaskH264;
    h.maxWidth = 1920;
    h.maxHeight = 1080;
    h.desiredFps = 60;
    h.sourceId = sourceId;
    const size_t n = BuildHello(buf, h);
    return Datagram(buf, buf + n);
}

void TestHelloRoutesBySourceId() {
    std::printf("[router] a HELLO goes to the source it names, not the first one...\n");
    auto a = MakePipe(0), b = MakePipe(1), c = MakePipe(2);
    SourcePipelineState* live[] = {a.get(), b.get(), c.get()};

    const Datagram hello = BuildHelloFor(1);
    const auto h = ParseCommonHeader(hello);
    Check(h.has_value(), "the HELLO parses");
    Check(RouteDatagram(live, *h, hello) == b.get(), "source 1 gets it");

    const Datagram other = BuildHelloFor(2);
    const auto h2 = ParseCommonHeader(other);
    Check(RouteDatagram(live, *h2, other) == c.get(), "source 2 gets its own");

    const Datagram absent = BuildHelloFor(9);
    const auto h3 = ParseCommonHeader(absent);
    Check(RouteDatagram(live, *h3, absent) == nullptr, "a HELLO for no source is dropped");
}

void TestSessionTrafficRoutesBySessionId() {
    std::printf("[router] everything else is routed by session id...\n");
    auto a = MakePipe(0), b = MakePipe(1);
    GiveSession(*a);
    GiveSession(*b);
    SourcePipelineState* live[] = {a.get(), b.get()};

    const Datagram hello = BuildHelloFor(1);
    HostCallbacks cb;
    std::vector<Datagram> out;
    cb.send = [&out](std::span<const uint8_t> d) { out.emplace_back(d.begin(), d.end()); };
    cb.randomBytes = TestRandomBytes;
    b->session = std::make_unique<HostSession>(cb, StreamParams{1920, 1080, 60, kStartBps});
    b->session->HandlePacket(hello, kT0);

    const uint32_t sid = b->session->sessionId();
    Check(sid != 0, "the session started");

    uint8_t bye[kCommonHeaderSize];
    const size_t n = BuildBye(bye, sid);
    const std::span<const uint8_t> pkt(bye, n);
    const auto h = ParseCommonHeader(pkt);
    Check(RouteDatagram(live, *h, pkt) == b.get(), "the owning session is found");

    uint8_t stray[kCommonHeaderSize];
    const size_t sn = BuildBye(stray, sid ^ 0xFFFF);
    const std::span<const uint8_t> strayPkt(stray, sn);
    const auto sh = ParseCommonHeader(strayPkt);
    Check(RouteDatagram(live, *sh, strayPkt) == nullptr, "an unknown session id routes nowhere");

    uint8_t sessionless[kCommonHeaderSize];
    const size_t zn = BuildBye(sessionless, 0);
    const std::span<const uint8_t> zeroPkt(sessionless, zn);
    const auto zh = ParseCommonHeader(zeroPkt);
    Check(RouteDatagram(live, *zh, zeroPkt) == nullptr, "session id 0 routes nowhere");
}

void TestAdoptPeerReportsOnlyChanges() {
    std::printf("[router] the peer address is logged when it changes, not every packet...\n");
    auto p = MakePipe(0);
    Check(AdoptPeer(*p, 0xC0A80001'0000ULL), "the first peer is new");
    Check(!AdoptPeer(*p, 0xC0A80001'0000ULL), "the same peer again is not");
    Check(AdoptPeer(*p, 0xC0A80002'0000ULL), "a different peer is");
    Check(p->peerPacked.load() == 0xC0A80002'0000ULL, "and the state follows");
}

void TestOfferRefreshNeedsAReason() {
    std::printf("[router] nothing is re-offered unless size or quality actually changed...\n");
    auto p = MakePipe(0);
    GiveSession(*p);
    p->srcW.store(1280);
    p->srcH.store(720);

    OfferUpdate u = RefreshOffer(*p, 60);
    Check(!u.sizeChanged && !u.qualityChanged && !u.sendReconfig, "an idle pipeline stays quiet");
    Check(p->offer.width == 0, "and the offer is left alone");

    p->sizeChanged.store(true);
    u = RefreshOffer(*p, 60);
    Check(u.sizeChanged, "the size change is reported");
    Check(p->offer.width == 1280 && p->offer.height == 720, "and the offer was refreshed");

    u = RefreshOffer(*p, 60);
    Check(!u.sizeChanged, "the flag is consumed, so it does not repeat");
}

void TestPausedSourceStillClearsItsFlags() {
    std::printf("[router] a paused source consumes its flags but re-offers nothing...\n");
    auto p = MakePipe(0);
    GiveSession(*p);
    p->srcW.store(1280);
    p->srcH.store(720);
    p->paused.store(true);
    p->sizeChanged.store(true);
    p->qualityChanged.store(true);

    const OfferUpdate u = RefreshOffer(*p, 60);
    Check(u.sizeChanged && u.qualityChanged, "the flags are still reported to the caller");
    Check(!u.sendReconfig, "but nothing goes on the wire");
    Check(p->offer.width == 0, "and the offer is untouched while paused");
    Check(!p->sizeChanged.load() && !p->qualityChanged.load(),
        "the flags were consumed, so they cannot pile up while paused");
}

void TestReconfigOnlyWithAStreamingPeer() {
    std::printf("[router] RECONFIG goes out only to a peer that is actually streaming...\n");
    auto p = MakePipe(0);
    GiveSession(*p);
    p->srcW.store(1600);
    p->srcH.store(900);
    p->curBitrateBps.store(9'000'000);
    p->step.fps = 30;

    p->sizeChanged.store(true);
    OfferUpdate u = RefreshOffer(*p, 60);
    Check(!u.sendReconfig, "with no peer there is nobody to reconfigure");
    Check(p->offer.fps == 30, "the offer still took the ladder's fps");

    p->peerPacked.store(0xC0A80001'0000ULL);
    p->sizeChanged.store(true);
    u = RefreshOffer(*p, 60);
    Check(!u.sendReconfig, "a peer that is not streaming yet gets nothing either");

    p->qualityChanged.store(true);
    u = RefreshOffer(*p, 60);
    Check(u.qualityChanged, "a quality change is reported all the same");
}

void TestOfferFallsBackToTheConfiguredFps() {
    std::printf("[router] before a ladder exists the offer uses the configured fps...\n");
    auto p = MakePipe(0);
    GiveSession(*p);
    p->srcW.store(800);
    p->srcH.store(600);
    p->step.fps = 0;
    p->sizeChanged.store(true);

    RefreshOffer(*p, 45);
    Check(p->offer.fps == 45, "the fallback is used when the ladder has not spoken");
}

void TestFlushTiming() {
    std::printf("[router] an idle source is nudged: IDR after 200 ms, keepalive after 500...\n");
    auto p = MakePipe(0);
    Check(DueForFlush(*p, kT0) == FlushReason::None, "with no session there is nothing to flush");

    GiveSession(*p);
    Check(DueForFlush(*p, kT0) == FlushReason::None, "an idle session is not streaming yet");
}

void TestFlushPrefersIdrOverKeepalive() {
    std::printf("[router] the flush reasons are ordered: a pending IDR wins...\n");
    SourcePipelineState st(kStartBps, kMinBps);
    HostCallbacks cb;
    cb.send = [](std::span<const uint8_t>) {};
    cb.randomBytes = TestRandomBytes;
    st.session = std::make_unique<HostSession>(cb, StreamParams{1920, 1080, 60, kStartBps});
    st.session->HandlePacket(BuildHelloFor(0), kT0);
    uint8_t start[kMaxDatagram];
    const size_t sn = BuildStart(start, st.session->sessionId());
    st.session->HandlePacket(std::span<const uint8_t>(start, sn), kT0);
    Check(st.session->state() == HostSession::State::Streaming, "the host is streaming");
    st.lastFrameUs.store(kT0);
    st.lastKeepaliveUs = kT0;

    Check(DueForFlush(st, kT0 + 100'000) == FlushReason::None,
        "a source that just sent a frame needs no nudge");

    st.forceIdr.store(true);
    Check(DueForFlush(st, kT0 + 100'000) == FlushReason::None,
        "a pending IDR still waits out the 200 ms idle window");
    Check(DueForFlush(st, kT0 + 250'000) == FlushReason::ForceIdr, "then it fires");

    st.forceIdr.store(false);
    Check(DueForFlush(st, kT0 + 250'000) == FlushReason::None,
        "with no IDR pending 250 ms is not yet a keepalive");
    Check(DueForFlush(st, kT0 + 600'000) == FlushReason::Keepalive, "600 ms is");

    st.lastKeepaliveUs = kT0 + 600'000;
    Check(DueForFlush(st, kT0 + 700'000) == FlushReason::None,
        "and having just sent one, it waits another interval");
}

void TestBeginNegotiation() {
    std::printf("[router] a HELLO builds the ladder and the first offer from it...\n");
    auto p = MakePipe(0);
    GiveSession(*p);
    p->curBitrateBps.store(15'000'000);

    Hello hello{};
    hello.maxWidth = 2560;
    hello.maxHeight = 1440;

    NegotiationHooks hooks;
    int calls = 0;
    uint16_t sawW = 0, sawH = 0;
    hooks.resolveSize = [&](uint16_t w, uint16_t h) {
        ++calls;
        sawW = w;
        sawH = h;
        return StreamSize{1920, 1080};
    };

    const NegotiationResult r = BeginNegotiation(*p, hello, 60, hooks);
    Check(calls == 1, "the platform is asked once how big the stream should be");
    Check(sawW == 2560 && sawH == 1440, "and it is told what the client can display");
    Check(r.accepted && r.size.width == 1920 && r.size.height == 1080, "the answer is reported");
    Check(r.rungCount > 0, "a ladder with at least one rung was built");
    Check(p->ladder != nullptr, "and the state holds it");
    Check(p->offer.width == 1920 && p->offer.height == 1080, "the offer matches");
    Check(p->offer.bitrateBps == 15'000'000, "and carries the current bitrate");
    Check(p->curFps.load() == p->step.fps, "curFps follows the rung the ladder starts on");
    Check(p->offer.fps == p->step.fps, "so does the offered fps");
}

void TestNegotiationRejectsAnUnusableSize() {
    std::printf("[router] if the platform cannot size the stream, nothing is offered...\n");
    auto p = MakePipe(0);
    GiveSession(*p);
    p->step.fps = 30;
    p->step.scalePct = 50;

    Hello hello{};
    hello.maxWidth = 1920;
    hello.maxHeight = 1080;

    NegotiationHooks hooks;
    hooks.resolveSize = [](uint16_t, uint16_t) { return StreamSize{0, 0}; };

    const NegotiationResult r = BeginNegotiation(*p, hello, 60, hooks);
    Check(!r.accepted, "the negotiation is refused");
    Check(p->ladder == nullptr, "no ladder was built");
    Check(p->offer.width == 0, "and no offer was made");
    Check(p->step == QualityStep{},
        "the previous rung is reset to the default, so a retry cannot inherit it");

    const NegotiationResult none = BeginNegotiation(*p, hello, 60, NegotiationHooks{});
    Check(!none.accepted, "a missing hook is refused rather than crashing");
}

}

void RunHostRouterTests() {
    TestHelloRoutesBySourceId();
    TestSessionTrafficRoutesBySessionId();
    TestAdoptPeerReportsOnlyChanges();
    TestOfferRefreshNeedsAReason();
    TestPausedSourceStillClearsItsFlags();
    TestReconfigOnlyWithAStreamingPeer();
    TestOfferFallsBackToTheConfiguredFps();
    TestFlushTiming();
    TestFlushPrefersIdrOverKeepalive();
    TestBeginNegotiation();
    TestNegotiationRejectsAnUnusableSize();
}
