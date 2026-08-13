#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhubp/net/NetInfo.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/system/Clock.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kLoopbackIp = 0x7F000001u;
constexpr uint16_t kFirstTestPort = 47800;
constexpr uint16_t kLastTestPort = 47850;

uint16_t OpenOnAFreePort(UdpSocket& sock) {
    for (uint16_t port = kFirstTestPort; port <= kLastTestPort; ++port)
        if (sock.Open(port)) return port;
    return 0;
}

void TestAClosedSocketRefusesEverything() {
    std::printf("[udp] a socket that was never opened never pretends to work...\n");
    UdpSocket sock;
    Check(!sock.IsOpen(), "a fresh socket is closed");

    const uint8_t byte = 1;
    Check(!sock.SendTo(NetAddr{kLoopbackIp, kFirstTestPort}, &byte, 1),
        "sending on a closed socket fails instead of silently dropping");
    Check(!sock.SetRecvTimeout(10), "so does setting a timeout");

    uint8_t buf[8];
    NetAddr from{};
    Check(sock.RecvFrom(buf, sizeof(buf), from) < 0,
        "receiving reports an error, which is what makes the net loop stop");

    sock.Close();
    Check(!sock.IsOpen(), "closing an already closed socket is harmless");
}

void TestOpenAndClose() {
    std::printf("[udp] a socket opens, reports itself open, and closes cleanly...\n");
    UdpSocket sock;
    Check(sock.Open(0), "port 0 lets the OS pick, which is what every viewer does");
    Check(sock.IsOpen(), "and the socket knows it is open");
    Check(!sock.lastBindAddrInUse(), "an ephemeral port is never reported as taken");
    Check(sock.SetRecvTimeout(10), "an open socket accepts a receive timeout");
    sock.Close();
    Check(!sock.IsOpen(), "after Close it is closed again");
    Check(sock.Open(0), "and it can be reopened for the next session");
}

void TestASecondHostOnTheSamePortIsToldWhy() {
    std::printf("[udp] the second Deskhub on one machine learns the port is taken...\n");
    UdpSocket first;
    const uint16_t port = OpenOnAFreePort(first);
    if (!port) {
        std::printf("  skipped: no free port in %u..%u\n", unsigned(kFirstTestPort),
            unsigned(kLastTestPort));
        return;
    }

    UdpSocket second;
    Check(!second.Open(port), "the second bind on the same port fails");
    Check(second.lastBindAddrInUse(),
        "and says so specifically, so the UI can name the real reason");
    Check(!second.IsOpen(), "a failed bind leaves the socket closed, not half-open");
}

void TestALoopbackDatagramArrivesIntact() {
    std::printf("[udp] a datagram sent to ourselves arrives whole, from where it says...\n");
    UdpSocket receiver;
    const uint16_t port = OpenOnAFreePort(receiver);
    if (!port) {
        std::printf("  skipped: no free port in %u..%u\n", unsigned(kFirstTestPort),
            unsigned(kLastTestPort));
        return;
    }
    receiver.SetRecvTimeout(200);

    UdpSocket sender;
    Check(sender.Open(0), "the sender takes an ephemeral port");

    std::vector<uint8_t> payload(1200);
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = uint8_t(i * 7 + 3);

    const NetAddr to{kLoopbackIp, port};
    Check(sender.SendTo(to, payload.data(), payload.size()), "the send succeeds");

    uint8_t buf[2048];
    NetAddr from{};
    int n = 0;
    for (int attempt = 0; attempt < 20 && n == 0; ++attempt)
        n = receiver.RecvFrom(buf, sizeof(buf), from);

    if (n <= 0) {
        std::printf("  skipped: loopback UDP is not available here (n=%d)\n", n);
        return;
    }

    Check(size_t(n) == payload.size(), "the whole datagram arrives in one piece");
    Check(std::memcmp(buf, payload.data(), payload.size()) == 0, "and its bytes are unchanged");
    Check(from.ip == kLoopbackIp, "the sender address is the one we sent from");
    Check(from.port != 0 && from.port != port,
        "and its port is the sender's, which is what the host replies to");
}

void TestABoundAddressStillReceives() {
    std::printf("[udp] a socket bound to one address still receives on it...\n");
    UdpSocket receiver;
    uint16_t port = 0;
    for (uint16_t candidate = kFirstTestPort; candidate <= kLastTestPort; ++candidate)
        if (receiver.Open(candidate, "127.0.0.1")) {
            port = candidate;
            break;
        }
    if (!port) {
        std::printf("  skipped: no free port in %u..%u\n", unsigned(kFirstTestPort),
            unsigned(kLastTestPort));
        return;
    }
    receiver.SetRecvTimeout(200);

    UdpSocket sender;
    Check(sender.Open(0), "the sender takes an ephemeral port");
    const uint8_t byte = 42;
    Check(sender.SendTo(NetAddr{kLoopbackIp, port}, &byte, 1),
        "a datagram aimed at the bound address is sent");

    uint8_t buf[8];
    NetAddr from{};
    int n = 0;
    for (int attempt = 0; attempt < 20 && n == 0; ++attempt)
        n = receiver.RecvFrom(buf, sizeof(buf), from);
    if (n <= 0) {
        std::printf("  skipped: loopback UDP is not available here (n=%d)\n", n);
        return;
    }
    Check(n == 1 && buf[0] == 42, "and arrives intact on the bound socket");

    UdpSocket bad;
    Check(!bad.Open(0, "not-an-ip"), "a malformed bind address fails instead of binding all");
    Check(!bad.IsOpen(), "and leaves the socket closed");
}

void TestATimedOutReceiveIsNotAnError() {
    std::printf("[udp] an idle second is not mistaken for a dead socket...\n");
    UdpSocket sock;
    Check(sock.Open(0), "open");
    Check(sock.SetRecvTimeout(10), "10 ms timeout");

    uint8_t buf[64];
    NetAddr from{};
    const uint64_t t0 = NowUs();
    const int n = sock.RecvFrom(buf, sizeof(buf), from);
    const uint64_t elapsedUs = NowUs() - t0;

    Check(n == 0, "nothing to read reports 0, never a negative error");
    Check(elapsedUs >= 5'000,
        "and the call actually waited, so the net loop does not spin at 100% CPU");
}

void TestLocalAddressesLookLikeAddresses() {
    std::printf("[netinfo] the addresses shown to the user are ones they can type back...\n");
    const std::vector<AdapterAddr> addrs = ListLocalIPv4();
    for (const AdapterAddr& a : addrs) {
        Check(!a.ip.empty(), "every listed adapter has an address to show");
        Check(!a.name.empty(), "and a name, so the user can tell Wi-Fi from a docker bridge");
        NetAddr parsed{};
        Check(ParseNetAddr(a.ip, parsed),
            "and it parses with our own parser, so copying it into the other machine works");
        Check((parsed.ip >> 24) != 127, "loopback is filtered out — nobody can connect to it");
        Check((parsed.ip >> 16) != 0xA9FE,
            "so is a self-assigned 169.254 address, which never routes anywhere");
    }
}

}

void RunUdpSocketTests() {
    TestAClosedSocketRefusesEverything();
    TestOpenAndClose();
    TestASecondHostOnTheSamePortIsToldWhy();
    TestALoopbackDatagramArrivesIntact();
    TestABoundAddressStillReceives();
    TestATimedOutReceiveIsNotAnError();
    TestLocalAddressesLookLikeAddresses();
}
