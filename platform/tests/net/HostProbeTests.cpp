#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhubp/client/DeviceStatusPoller.h"
#include "deskhubp/client/HostProbe.h"
#include "deskhubp/system/Clock.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr uint32_t kLoopbackIp = 0x7F000001u;
constexpr uint16_t kFirstProbePort = 47830;
constexpr uint16_t kLastProbePort = 47858;
constexpr uint16_t kSilentPort = 47859;

uint16_t OpenOnAFreeProbePort(UdpSocket& sock) {
    for (uint16_t port = kFirstProbePort; port <= kLastProbePort; ++port)
        if (sock.Open(port)) return port;
    return 0;
}

void AnswerQueriesFor(UdpSocket& host, uint64_t forUs) {
    host.SetRecvTimeout(100);
    uint8_t buf[deskhub::kMaxDatagram];
    const uint64_t startUs = NowUs();
    while (NowUs() - startUs < forUs) {
        NetAddr from;
        const int n = host.RecvFrom(buf, sizeof(buf), from);
        if (n <= 0) continue;
        const auto h = deskhub::ParseCommonHeader(std::span<const uint8_t>(buf, size_t(n)));
        if (!h || h->type != deskhub::MsgType::ListSources) continue;

        deskhub::SourceInfo source;
        source.sourceId = 1;
        source.width = 1920;
        source.height = 1080;
        source.name = "Screen 1";
        uint8_t reply[deskhub::kMaxDatagram];
        const size_t rn = deskhub::BuildSourceList(reply, std::span(&source, 1));
        if (rn) host.SendTo(from, reply, rn);
    }
}

void TestProbeFindsALiveHost() {
    std::printf("[probe] a sharing host on loopback answers with a measurable rtt...\n");
    UdpSocket host;
    const uint16_t port = OpenOnAFreeProbePort(host);
    Check(port != 0, "a loopback host socket could be opened");
    if (!port) return;

    std::thread responder(AnswerQueriesFor, std::ref(host), uint64_t{2'000'000});
    const auto rtt = deskhubp::ProbeHostRttMs(NetAddr{kLoopbackIp, port}, 2000);
    responder.join();

    Check(rtt.has_value(), "the live host is reported online");
    Check(!rtt || *rtt >= 1, "an rtt of zero is never reported");
}

void TestProbeTimesOutQuietly() {
    std::printf("[probe] a silent host means offline within the timeout, not a hang...\n");
    const uint64_t startUs = NowUs();
    const auto rtt = deskhubp::ProbeHostRttMs(NetAddr{kLoopbackIp, kSilentPort}, 300);
    const uint64_t tookUs = NowUs() - startUs;
    Check(!rtt.has_value(), "silence is reported as offline");
    Check(tookUs < 2'000'000, "the probe gives up in bounded time");
}

void TestBatchProbeKeepsIndexes() {
    std::printf("[probe] one round probes many hosts and keeps results in order...\n");
    UdpSocket host;
    const uint16_t port = OpenOnAFreeProbePort(host);
    Check(port != 0, "a loopback host socket could be opened");
    if (!port) return;

    std::thread responder(AnswerQueriesFor, std::ref(host), uint64_t{2'000'000});
    const NetAddr targets[2] = {{kLoopbackIp, kSilentPort}, {kLoopbackIp, port}};
    const auto rtts = deskhubp::ProbeHostsRttMs(targets, 1200);
    responder.join();

    Check(rtts.size() == 2, "one result per probed host");
    Check(!rtts[0].has_value(), "the silent host stays offline");
    Check(rtts[1].has_value(), "the live host is found in the same round");
}

void TestPollerReportsEveryAddress() {
    std::printf("[probe] the poller reports live, silent and junk addresses (~2 s)...\n");
    UdpSocket host;
    const uint16_t port = OpenOnAFreeProbePort(host);
    Check(port != 0, "a loopback host socket could be opened");
    if (!port) return;

    std::thread responder(AnswerQueriesFor, std::ref(host), uint64_t{6'000'000});

    const std::string liveAddr = "127.0.0.1:" + std::to_string(port);
    std::mutex mutex;
    std::condition_variable seenAll;
    std::map<std::string, deskhubp::DeviceStatus> statuses;

    deskhubp::DeviceStatusPoller poller;
    poller.SetAddresses({liveAddr, "not an address"});
    poller.Start([&](const deskhubp::DeviceStatus& st) {
        std::lock_guard lock(mutex);
        statuses[st.addr] = st;
        seenAll.notify_all();
    });

    {
        std::unique_lock lock(mutex);
        seenAll.wait_for(lock, std::chrono::seconds(5), [&] { return statuses.size() >= 2; });
    }
    poller.Stop();
    responder.join();

    Check(statuses.size() == 2, "every configured address got a status report");
    Check(!statuses["not an address"].online, "junk input degrades to offline, not a crash");
    Check(statuses[liveAddr].online && statuses[liveAddr].rttMs >= 1,
        "the live host on a custom port is online with a real rtt");
}

}

void RunHostProbeTests() {
    TestProbeFindsALiveHost();
    TestProbeTimesOutQuietly();
    TestBatchProbeKeepsIndexes();
    TestPollerReportsEveryAddress();
}
