#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/session/ClientPump.h"
#include "deskhub/session/HostSession.h"
#include "deskhub/transport/Packetizer.h"

#include <cstdio>
#include <deque>
#include <string>
#include <vector>

using namespace deskhub;

namespace {

struct Rig {
    std::deque<Datagram> toHost, toClient;

    diag::ClientDiag diag;
    std::vector<Reassembler::Frame> frames;
    std::vector<std::string> logs;
    std::string status;
    std::string ended;
    NegotiatedParams params{};
    int paramsCalls = 0;
    int reconfigCalls = 0;
    uint32_t renderedToReport = 0;
    int64_t latency = -1;

    ClientPumpCallbacks Callbacks() {
        ClientPumpCallbacks cb;
        cb.send = [this](std::span<const uint8_t> d) { toHost.emplace_back(d.begin(), d.end()); };
        cb.onFrame = [this](Reassembler::Frame&& f) { frames.push_back(std::move(f)); };
        cb.onParams = [this](const NegotiatedParams& p, bool reconfigured) {
            params = p;
            ++paramsCalls;
            if (reconfigured) ++reconfigCalls;
        };
        cb.onEnded = [this](const char* reason) { ended = reason ? reason : ""; };
        cb.takeRenderedCount = [this] {
            const uint32_t n = renderedToReport;
            renderedToReport = 0;
            return n;
        };
        cb.latencyUs = [this] { return latency; };
        cb.onStatus = [this](const char* s) { status = s ? s : ""; };
        cb.localTime = [] { return std::string("12:34:56"); };
        cb.log = [this](bool, const char* line) { logs.emplace_back(line ? line : ""); };
        return cb;
    }

    bool LoggedContaining(const char* needle) const {
        for (const auto& l : logs)
            if (l.find(needle) != std::string::npos) return true;
        return false;
    }
};

size_t CountToHost(const std::deque<Datagram>& q, MsgType type) {
    size_t n = 0;
    for (const auto& d : q) {
        const auto h = ParseCommonHeader(d);
        if (h && h->type == type) ++n;
    }
    return n;
}

struct Connected {
    Rig rig;
    ClientPump pump;
    HostSession host;
    uint64_t now = 10'000'000;

    Connected(HostCallbacks hcb, StreamParams offer)
        : pump(rig.Callbacks(), rig.diag), host(hcb, offer) {}
};

void Exchange(Rig& r, ClientPump& pump, HostSession& host, uint64_t now) {
    for (int guard = 0; guard < 8; ++guard) {
        if (r.toHost.empty() && r.toClient.empty()) break;
        while (!r.toHost.empty()) {
            auto d = std::move(r.toHost.front());
            r.toHost.pop_front();
            host.HandlePacket(d, now);
        }
        while (!r.toClient.empty()) {
            auto d = std::move(r.toClient.front());
            r.toClient.pop_front();
            pump.OnDatagram(d, now);
        }
    }
}

void TestHandshakeAndParams() {
    std::printf("[pump] Start() negotiates and reports the agreed parameters...\n");
    Rig r;
    ClientPump pump(r.Callbacks(), r.diag);

    HostCallbacks hcb;
    hcb.send = [&](std::span<const uint8_t> d) { r.toClient.emplace_back(d.begin(), d.end()); };
    hcb.randomBytes = TestRandomBytes;
    HostSession host(hcb, StreamParams{1920, 1080, 60, 20'000'000});

    uint64_t now = 10'000'000;
    pump.Start(ClientPumpConfig{0x11223344, 2560, 1440, 0, 60}, now);
    Check(CountToHost(r.toHost, MsgType::Hello) == 1, "Start() puts a HELLO on the wire");

    Exchange(r, pump, host, now);
    Check(r.paramsCalls == 1 && r.reconfigCalls == 0, "onParams fires once, not as a reconfig");
    Check(r.params.width == 1920 && r.params.height == 1080 && r.params.fps == 60,
        "the negotiated size and rate are handed to the client");
    Check(host.state() == HostSession::State::Streaming, "the host went streaming");
    Check(r.LoggedContaining("Negotiation done"), "and it was logged");
}

void TestVideoReachesTheFrameSink() {
    std::printf("[pump] reassembled frames are handed to the decoder, IDRs cancel requests...\n");
    Rig r;
    ClientPump pump(r.Callbacks(), r.diag);

    HostCallbacks hcb;
    hcb.send = [&](std::span<const uint8_t> d) { r.toClient.emplace_back(d.begin(), d.end()); };
    hcb.randomBytes = TestRandomBytes;
    HostSession host(hcb, StreamParams{1920, 1080, 60, 20'000'000});

    uint64_t now = 10'000'000;
    pump.Start(ClientPumpConfig{1, 1920, 1080, 0, 60}, now);
    Exchange(r, pump, host, now);

    Packetizer pk;
    pk.SetSessionId(host.sessionId());
    const TestFrame idr = MakeIdrFrame(0, 4);
    for (const auto& d : Packetize(pk, idr, now)) pump.OnDatagram(d, now);

    now += 20'000;
    pump.PollFrames(now);
    Check(r.frames.size() == 1, "the frame came out whole");
    Check(SameFrame(r.frames[0], idr), "and byte-identical to what the host sent");
    Check(r.frames[0].idr, "marked as an IDR");
    Check(pump.streaming(), "video traffic moves the session to streaming");
}

void TestKeyframeRequestsAreLoggedOnce() {
    std::printf("[pump] a keyframe request is logged on the edge, not on every repeat...\n");
    Rig r;
    ClientPump pump(r.Callbacks(), r.diag);

    HostCallbacks hcb;
    hcb.send = [&](std::span<const uint8_t> d) { r.toClient.emplace_back(d.begin(), d.end()); };
    hcb.randomBytes = TestRandomBytes;
    bool hostSawRequest = false;
    hcb.onKeyframeRequest = [&] { hostSawRequest = true; };
    HostSession host(hcb, StreamParams{1920, 1080, 60, 20'000'000});

    uint64_t now = 10'000'000;
    pump.Start(ClientPumpConfig{1, 1920, 1080, 0, 60}, now);
    Exchange(r, pump, host, now);

    Packetizer pk;
    pk.SetSessionId(host.sessionId());
    for (const auto& d : Packetize(pk, MakeIdrFrame(0, 2), now)) pump.OnDatagram(d, now);
    Check(pump.streaming(), "video traffic is what makes a keyframe request sendable");
    r.logs.clear();

    pump.RequestKeyframe("dec_fail", now);
    const size_t afterFirst = r.logs.size();
    Check(afterFirst == 1, "the first request is logged");
    Check(r.LoggedContaining("dec_fail"), "with its reason");

    pump.RequestKeyframe("q_overflow", now + 1000);
    Check(r.logs.size() == afterFirst, "a second request while one is pending stays quiet");

    r.toHost.clear();
    now += 300'000;
    pump.Tick(now);
    Exchange(r, pump, host, now);
    Check(hostSawRequest, "the host did receive the request");
}

void TestReportRunsOncePerWindow() {
    std::printf("[pump] the status line and FEEDBACK go out once a second...\n");
    Rig r;
    ClientPump pump(r.Callbacks(), r.diag);

    HostCallbacks hcb;
    hcb.send = [&](std::span<const uint8_t> d) { r.toClient.emplace_back(d.begin(), d.end()); };
    hcb.randomBytes = TestRandomBytes;
    Feedback got{};
    int feedbacks = 0;
    hcb.onFeedback = [&](const Feedback& f) {
        got = f;
        ++feedbacks;
    };
    HostSession host(hcb, StreamParams{1920, 1080, 60, 20'000'000});

    uint64_t now = 10'000'000;
    pump.Start(ClientPumpConfig{1, 1920, 1080, 0, 60}, now);
    Exchange(r, pump, host, now);

    Packetizer pk;
    pk.SetSessionId(host.sessionId());
    for (const auto& d : Packetize(pk, MakeIdrFrame(0, 4), now)) pump.OnDatagram(d, now);
    pump.PollFrames(now);
    r.renderedToReport = 60;
    r.latency = 25'000;

    Check(r.status.empty(), "no status before the first window closes");
    now += 400'000;
    Check(pump.Tick(now), "mid-window tick keeps the session alive");
    Check(r.status.empty(), "and reports nothing");

    r.toHost.clear();
    now += 700'000;
    Check(pump.Tick(now), "the window closes");
    Check(!r.status.empty(), "a compact status line reached the UI");
    Check(CountToHost(r.toHost, MsgType::Feedback) == 1, "exactly one FEEDBACK went out");

    Exchange(r, pump, host, now);
    Check(feedbacks == 1 && got.recvBitrateKbps > 0,
        "the host got it, carrying the bytes actually received");

    Check(r.status.find("  ") != std::string::npos,
        "the default separator is two spaces");
}

void TestStatusSeparatorIsConfigurable() {
    std::printf("[pump] the UI can ask for its own separator in the status line...\n");
    Rig r;
    ClientPump pump(r.Callbacks(), r.diag);

    HostCallbacks hcb;
    hcb.send = [&](std::span<const uint8_t> d) { r.toClient.emplace_back(d.begin(), d.end()); };
    hcb.randomBytes = TestRandomBytes;
    HostSession host(hcb, StreamParams{1920, 1080, 60, 20'000'000});

    uint64_t now = 10'000'000;
    ClientPumpConfig cfg{1, 1920, 1080, 0, 60};
    cfg.statusSeparator = " | ";
    pump.Start(cfg, now);
    Exchange(r, pump, host, now);

    now += 1'100'000;
    Check(pump.Tick(now), "the window closes");
    Check(r.status.find(" | ") != std::string::npos, "the chosen separator is used");
    Check(r.status.find("fps") != std::string::npos, "and the fields are still there");
}

void TestDisconnectEndsTheLoop() {
    std::printf("[pump] a host BYE ends the pump and names the reason...\n");
    Rig r;
    ClientPump pump(r.Callbacks(), r.diag);

    HostCallbacks hcb;
    hcb.send = [&](std::span<const uint8_t> d) { r.toClient.emplace_back(d.begin(), d.end()); };
    hcb.randomBytes = TestRandomBytes;
    HostSession host(hcb, StreamParams{1920, 1080, 60, 20'000'000});

    uint64_t now = 10'000'000;
    pump.Start(ClientPumpConfig{1, 1920, 1080, 0, 60}, now);
    Exchange(r, pump, host, now);
    Check(r.ended.empty(), "still connected");

    uint8_t bye[kCommonHeaderSize];
    const size_t n = BuildBye(bye, host.sessionId());
    Check(n > 0, "built a BYE");
    pump.OnDatagram(std::span<const uint8_t>(bye, n), now);

    Check(!r.ended.empty(), "onEnded reported a reason");
    Check(!pump.Tick(now), "and Tick() tells the caller to stop looping");
}

void TestLoopBusyWarning() {
    std::printf("[pump] a slow loop iteration is counted and warned about...\n");
    Rig r;
    ClientPump pump(r.Callbacks(), r.diag);
    pump.Start(ClientPumpConfig{1, 1920, 1080, 0, 60}, 10'000'000);
    r.logs.clear();

    pump.CountLoopBusy(10'000'000, 10'001'000);
    Check(r.logs.empty(), "a 1 ms iteration is unremarkable");

    pump.CountLoopBusy(10'000'000, 10'000'000 + (kLoopStallWarnMs + 10) * 1000);
    Check(r.LoggedContaining("recv_stall"), "a long one is reported");
}

void TestStrayTrafficIsIgnored() {
    std::printf("[pump] video for another session is dropped, not reassembled...\n");
    Rig r;
    ClientPump pump(r.Callbacks(), r.diag);

    HostCallbacks hcb;
    hcb.send = [&](std::span<const uint8_t> d) { r.toClient.emplace_back(d.begin(), d.end()); };
    hcb.randomBytes = TestRandomBytes;
    HostSession host(hcb, StreamParams{1920, 1080, 60, 20'000'000});

    uint64_t now = 10'000'000;
    pump.Start(ClientPumpConfig{1, 1920, 1080, 0, 60}, now);
    Exchange(r, pump, host, now);

    Packetizer stray;
    stray.SetSessionId(host.sessionId() ^ 0xFFFF);
    for (const auto& d : Packetize(stray, MakeIdrFrame(0, 2), now)) pump.OnDatagram(d, now);
    pump.PollFrames(now + 20'000);
    Check(r.frames.empty(), "nothing was handed to the decoder");

    const uint8_t garbage[3] = {1, 2, 3};
    pump.OnDatagram(garbage, now);
    Check(r.frames.empty(), "a runt datagram is harmless too");
}

}

void RunClientPumpTests() {
    TestHandshakeAndParams();
    TestVideoReachesTheFrameSink();
    TestKeyframeRequestsAreLoggedOnce();
    TestReportRunsOncePerWindow();
    TestStatusSeparatorIsConfigurable();
    TestDisconnectEndsTheLoop();
    TestLoopBusyWarning();
    TestStrayTrafficIsIgnored();
}
