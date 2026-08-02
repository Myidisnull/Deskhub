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

void GiveStreamingSession(SourcePipelineState& st) {
    GiveSession(st);
    st.session->HandlePacket(BuildHelloFor(0), kT0);
    uint8_t start[kMaxDatagram];
    const size_t sn = BuildStart(start, st.session->sessionId());
    st.session->HandlePacket(std::span<const uint8_t>(start, sn), kT0);
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

void TestAcceptDatagramDoesTheWholeIntake() {
    std::printf("[router] one call parses, routes, feeds the session and adopts the peer...\n");
    constexpr uint64_t kPeer = 0xC0A80005'0000ULL;
    auto a = MakePipe(0), b = MakePipe(1);
    GiveSession(*a);
    GiveSession(*b);
    SourcePipelineState* live[] = {a.get(), b.get()};

    const Datagram hello = BuildHelloFor(1);
    AcceptedDatagram acc = AcceptDatagram(live, hello, kPeer, kT0);
    Check(acc.parsed, "the datagram parsed");
    Check(acc.target == b.get(), "it reached the source it named");
    Check(acc.peerChanged, "the first peer is adopted");
    Check(b->peerPacked.load() == kPeer, "and recorded on the pipeline");
    Check(a->peerPacked.load() == 0, "the other source is untouched");

    acc = AcceptDatagram(live, hello, kPeer, kT0);
    Check(acc.target == b.get(), "a repeat still routes");
    Check(!acc.peerChanged, "but the peer is not re-announced");

    const Datagram absent = BuildHelloFor(9);
    acc = AcceptDatagram(live, absent, kPeer, kT0);
    Check(acc.parsed, "a HELLO for an unknown source still parses");
    Check(acc.target == nullptr && !acc.peerChanged, "but routes nowhere");

    const Datagram runt(3, 0);
    acc = AcceptDatagram(live, runt, kPeer, kT0);
    Check(!acc.parsed && acc.target == nullptr, "a runt is rejected before anything else");

    b->failed.store(true);
    acc = AcceptDatagram(live, hello, 0xDEAD'0000ULL, kT0);
    Check(acc.parsed && acc.target == nullptr, "a failed source accepts nothing");
    Check(b->peerPacked.load() == kPeer, "and keeps the peer it had");
}

void TestReplyAddressIsStampedBeforeTheSessionReplies() {
    std::printf("[router] the reply address is the sender's, stamped before any reply...\n");
    constexpr uint64_t kPeer = 0xC0A80009'0000ULL;
    auto p = MakePipe(0);

    uint64_t replySeenAtSendTime = 0;
    HostCallbacks cb;
    cb.send = [&](std::span<const uint8_t>) {
        replySeenAtSendTime = p->replyPacked.load();
    };
    cb.randomBytes = TestRandomBytes;
    p->session = std::make_unique<HostSession>(cb, StreamParams{1920, 1080, 60, kStartBps});
    SourcePipelineState* live[] = {p.get()};

    const Datagram hello = BuildHelloFor(0);
    const AcceptedDatagram acc = AcceptDatagram(live, hello, kPeer, kT0);
    Check(acc.target == p.get(), "the HELLO reached its source");
    Check(replySeenAtSendTime == kPeer,
        "the HELLO_ACK went back to the sender of this very datagram");

    constexpr uint64_t kOther = 0xC0A8000A'0000ULL;
    AcceptDatagram(live, hello, kOther, kT0 + 1);
    Check(replySeenAtSendTime == kOther, "a retry from a new address is answered there");
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

void TestReconfigReachesAStreamingPeer() {
    std::printf("[router] a streaming peer is told about the new offer...\n");
    auto p = MakePipe(0);
    GiveStreamingSession(*p);
    p->peerPacked.store(0xC0A80001'0000ULL);
    p->srcW.store(1600);
    p->srcH.store(900);
    p->curBitrateBps.store(9'000'000);
    p->step.fps = 30;

    p->sizeChanged.store(true);
    const OfferUpdate u = RefreshOffer(*p, 60);
    Check(u.sendReconfig, "size change + streaming peer -> RECONFIG");
    Check(u.reconfig.width == 1600 && u.reconfig.height == 900,
        "the RECONFIG carries the new size");
    Check(u.reconfig.fps == 30 && u.reconfig.bitrateBps == 9'000'000,
        "and the ladder's rate");
}

void TestZeroSizedCaptureIsDropped() {
    std::printf("[router] a capture with no size is dropped before it hurts anything...\n");
    auto p = MakePipe(0);
    FrameAdmission a = AdmitCapturedFrame(*p, 0, 1080, 0);
    Check(a.drop, "zero width is dropped");
    a = AdmitCapturedFrame(*p, 1920, 0, 0);
    Check(a.drop, "zero height is dropped");
    Check(p->srcW.load() == 0, "and no encode size was ever adopted");

    a = AdmitCapturedFrame(*p, 1, 1080, 0);
    Check(a.drop, "a one-pixel-wide capture rounds to nothing and is dropped");
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
    GiveStreamingSession(st);
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

void TestTakingAFlushRestartsTheKeepaliveInterval() {
    std::printf("[router] taking a flush reason is what restarts the keepalive interval...\n");
    SourcePipelineState st(kStartBps, kMinBps);
    GiveStreamingSession(st);
    st.lastFrameUs.store(kT0);
    st.lastKeepaliveUs = kT0;

    Check(TakeFlushReason(st, kT0 + 600'000) == FlushReason::Keepalive, "600 ms idle is due");
    Check(st.lastKeepaliveUs == kT0 + 600'000, "and taking it stamps the clock");
    Check(TakeFlushReason(st, kT0 + 700'000) == FlushReason::None,
        "so the next tick is inside the interval and asks for nothing");

    st.forceIdr.store(true);
    Check(TakeFlushReason(st, kT0 + 900'000) == FlushReason::ForceIdr, "a pending IDR is due");
    Check(st.lastKeepaliveUs == kT0 + 900'000,
        "an IDR flush restarts the interval too \xE2\x80\x94 the viewer just got a frame");

    st.forceIdr.store(false);
    Check(TakeFlushReason(st, kT0 + 1'000'000) == FlushReason::None,
        "so no keepalive follows it inside the interval");

    Check(DueForFlush(st, kT0 + 1'500'000) == FlushReason::Keepalive,
        "asking without taking leaves the clock alone");
    Check(st.lastKeepaliveUs == kT0 + 900'000, "DueForFlush stays a pure question");
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

void TestStatusProjection() {
    std::printf("[router] a pipeline projects into the UI row and the beacon entry...\n");
    auto p = MakePipe(3);
    p->name = "Display 1";
    p->srcW.store(1920);
    p->srcH.store(1080);
    p->uiRttMs.store(17);
    p->statWindow.captureFps = 59.5;
    p->statWindow.sendFps = 58.25;
    p->statWindow.sendKbps = 12'500;

    const media::AgentSourceStatus idle =
        MakeSourceStatus(*p, StatusExtras{"192.168.1.7:47777", false});
    Check(!idle.viewerConnected, "no peer adopted yet");
    Check(idle.viewerAddr.empty(),
        "and the address is dropped, so a stale peer cannot show up in the UI");
    Check(idle.sourceId == 3 && idle.name == "Display 1", "identity is carried over");
    Check(idle.width == 1920 && idle.height == 1080, "so is the encoded size");
    Check(idle.rttMs == 17, "and the link RTT");
    Check(idle.captureFps == 59.5 && idle.sendFps == 58.25 && idle.sendKbps == 12'500,
        "the closed stats window is carried over verbatim");
    Check(!idle.zeroCopy, "zero-copy is reported by the caller, not inferred");

    p->peerPacked.store(0xC0A80107ull);
    const media::AgentSourceStatus live =
        MakeSourceStatus(*p, StatusExtras{"192.168.1.7:47777", true});
    Check(live.viewerConnected, "an adopted peer marks the source as viewed");
    Check(live.viewerAddr == "192.168.1.7:47777", "and the formatted address comes through");
    Check(live.zeroCopy, "the zero-copy flag is passed through");

    const SourceInfo info = MakeSourceInfo(*p);
    Check(info.sourceId == 3 && info.name == "Display 1", "the beacon entry keeps the identity");
    Check(info.width == 1920 && info.height == 1080, "and the advertised size");
}

}

void RunHostRouterTests() {
    TestHelloRoutesBySourceId();
    TestSessionTrafficRoutesBySessionId();
    TestAdoptPeerReportsOnlyChanges();
    TestAcceptDatagramDoesTheWholeIntake();
    TestReplyAddressIsStampedBeforeTheSessionReplies();
    TestOfferRefreshNeedsAReason();
    TestPausedSourceStillClearsItsFlags();
    TestReconfigOnlyWithAStreamingPeer();
    TestReconfigReachesAStreamingPeer();
    TestZeroSizedCaptureIsDropped();
    TestOfferFallsBackToTheConfiguredFps();
    TestFlushTiming();
    TestFlushPrefersIdrOverKeepalive();
    TestTakingAFlushRestartsTheKeepaliveInterval();
    TestBeginNegotiation();
    TestNegotiationRejectsAnUnusableSize();
    TestStatusProjection();
}
