#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhubp/net/SourceQuery.h"
#include "deskhubp/system/Clock.h"

#include <cstdio>
#include <span>
#include <thread>
#include <vector>

namespace {

constexpr uint32_t kLoopbackIp = 0x7F000001u;
constexpr uint16_t kFirstTestPort = 47860;
constexpr uint16_t kLastTestPort = 47890;

uint16_t OpenOnAFreePort(UdpSocket& sock) {
    for (uint16_t port = kFirstTestPort; port <= kLastTestPort; ++port)
        if (sock.Open(port)) return port;
    return 0;
}

std::vector<deskhub::SourceInfo> TwoSources() {
    std::vector<deskhub::SourceInfo> s(2);
    s[0].sourceId = 1;
    s[0].width = 1920;
    s[0].height = 1080;
    s[0].name = "Screen 1";
    s[1].sourceId = 2;
    s[1].width = 2560;
    s[1].height = 1440;
    s[1].name = "Screen 2";
    return s;
}

void AnswerOneQuery(UdpSocket& host) {
    host.SetRecvTimeout(200);
    uint8_t buf[deskhub::kMaxDatagram];
    const uint64_t startUs = NowUs();
    while (NowUs() - startUs < 2'500'000) {
        NetAddr from;
        const int n = host.RecvFrom(buf, sizeof(buf), from);
        if (n <= 0) continue;
        const auto h = deskhub::ParseCommonHeader(std::span<const uint8_t>(buf, size_t(n)));
        if (!h || h->type != deskhub::MsgType::ListSources) continue;

        const uint8_t garbage[3] = {1, 2, 3};
        host.SendTo(from, garbage, sizeof(garbage));

        uint8_t reply[deskhub::kMaxDatagram];
        size_t rn = deskhub::BuildBye(reply, 0x1234);
        if (rn) host.SendTo(from, reply, rn);

        const std::vector<deskhub::SourceInfo> sources = TwoSources();
        rn = deskhub::BuildSourceList(reply, sources);
        if (rn) host.SendTo(from, reply, rn);
        return;
    }
}

void TestQueryGetsTheList() {
    std::printf("[srcq] a LIST_SOURCES query returns what the host is sharing...\n");
    UdpSocket host;
    const uint16_t port = OpenOnAFreePort(host);
    Check(port != 0, "a loopback host socket could be opened");
    if (!port) return;

    std::thread responder(AnswerOneQuery, std::ref(host));

    std::vector<deskhub::SourceInfo> out;
    const bool ok = QuerySources(NetAddr{kLoopbackIp, port}, out);
    responder.join();

    Check(ok, "the query succeeded despite garbage and stray replies first");
    Check(out.size() == 2, "both sources came back");
    if (out.size() == 2) {
        Check(out[0].sourceId == 1 && out[0].width == 1920 && out[0].height == 1080,
            "the first source survives the round trip");
        Check(out[1].name == "Screen 2", "so does the second source's name");
    }
}

void TestQueryTimesOutWithoutAHost() {
    std::printf("[srcq] no host answering means a clean false, not a hang (~3 s)...\n");
    std::vector<deskhub::SourceInfo> out;
    out.push_back(deskhub::SourceInfo{});
    const uint64_t startUs = NowUs();
    const bool ok = QuerySources(NetAddr{kLoopbackIp, kLastTestPort + 1}, out);
    const uint64_t tookUs = NowUs() - startUs;
    Check(!ok, "silence is reported as failure");
    Check(out.empty(), "and the stale result list was cleared");
    Check(tookUs < 10'000'000, "the query gives up in bounded time");
}

}

void RunSourceQueryTests() {
    TestQueryGetsTheList();
    TestQueryTimesOutWithoutAHost();
}
