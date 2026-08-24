#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhubp/client/ScreenViewerLoop.h"
#include "deskhubp/net/UdpSocket.h"

#include <cstdio>
#include <set>
#include <string>

namespace {

NetAddr Addr(uint32_t ip, uint16_t port) {
    return NetAddr{ip, port};
}

void TestParsingAcceptsAnIPv4WithDefaultPort() {
    std::printf("[netaddr] a bare IP parses and gets the default port...\n");

    NetAddr a{};
    Check(ParseNetAddr("192.168.1.10", a), "a dotted quad parses");
    Check(a.ip == 0xC0A8010Au, "and lands in host byte order, high octet first");
    Check(a.port == kDeskhubPort, "no port given means the default Deskhub port");

    Check(ParseNetAddr("0.0.0.0", a) && a.ip == 0, "the all-zero address parses");
    Check(ParseNetAddr("255.255.255.255", a) && a.ip == 0xFFFFFFFFu, "so does the broadcast one");
    Check(ParseNetAddr("127.0.0.1", a) && a.ip == 0x7F000001u, "so does loopback");

    const char* bad[] = {"", "localhost", "192.168.1", "192.168.1.256", "192.168.1.10 ",
        "1.2.3.4.5", "::1", "hello"};
    for (const char* s : bad) {
        NetAddr out{0xDEADBEEFu, 1234};
        Check(!ParseNetAddr(s, out), "anything that is not an IPv4 address is refused");
    }
}

void TestParsingAcceptsAnOptionalPort() {
    std::printf("[netaddr] an explicit host:port is honoured, a broken one refused...\n");
    NetAddr a{};
    Check(ParseNetAddr("192.168.1.10:1234", a) && a.ip == 0xC0A8010Au && a.port == 1234,
        "the port the user typed is the port that is used");
    Check(ParseNetAddr("127.0.0.1:65535", a) && a.port == 65535, "the highest port works");
    Check(ParseNetAddr("10.0.0.7:1", a) && a.port == 1, "so does the lowest");

    const char* bad[] = {":47777", "192.168.1.10:", "192.168.1.10:0", "192.168.1.10:65536",
        "192.168.1.10:12:34", "192.168.1.10:abc", "192.168.1.10:12b"};
    for (const char* s : bad) {
        NetAddr out{};
        Check(!ParseNetAddr(s, out), "a malformed port is refused, never half-understood");
    }
}

void TestToStringIsWhatTheUserTyped() {
    std::printf("[netaddr] an address prints back the way it was written...\n");
    Check(Addr(0xC0A8010Au, kDeskhubPort).ToString() == "192.168.1.10:47777",
        "octets are printed high to low, with the port");
    Check(Addr(0, 0).ToString() == "0.0.0.0:0", "an unset address is obvious, not blank");
    Check(Addr(0xFFFFFFFFu, 65535).ToString() == "255.255.255.255:65535",
        "the widest address still fits the buffer");

    NetAddr parsed{};
    Check(ParseNetAddr("10.0.0.7", parsed), "parse");
    Check(parsed.ToString() == "10.0.0.7:47777", "parse then print round-trips");
}

void TestPackingSurvivesTheRoundTrip() {
    std::printf("[netaddr] the peer address stored in an atomic comes back intact...\n");
    const NetAddr addrs[] = {Addr(0, 0), Addr(0x7F000001u, kDeskhubPort),
        Addr(0xFFFFFFFFu, 65535), Addr(0xC0A8010Au, 1)};
    for (const NetAddr& a : addrs) {
        const NetAddr back = NetAddr::Unpack(a.Pack());
        Check(back == a, "pack/unpack is lossless, so a peer is never misrouted");
    }

    Check(Addr(0, 0).Pack() == 0,
        "an unset peer packs to 0, which is what the host checks for 'no peer yet'");
    Check(Addr(0, 1).Pack() != 0, "a real peer never packs to the 'no peer' value");

    std::set<uint64_t> packed;
    for (uint32_t ip = 1; ip <= 4; ++ip)
        for (uint16_t port = 1; port <= 4; ++port) packed.insert(Addr(ip, port).Pack());
    Check(packed.size() == 16, "distinct addresses never collide into the same 64-bit key");
}

void TestEquality() {
    std::printf("[netaddr] two addresses match only when both halves match...\n");
    Check(Addr(1, 2) == Addr(1, 2), "same ip and port");
    Check(!(Addr(1, 2) == Addr(1, 3)), "a different port is a different peer");
    Check(!(Addr(1, 2) == Addr(2, 2)), "a different ip is a different peer");
}

}

void RunNetAddrTests() {
    TestParsingAcceptsAnIPv4WithDefaultPort();
    TestParsingAcceptsAnOptionalPort();
    TestToStringIsWhatTheUserTyped();
    TestPackingSurvivesTheRoundTrip();
    TestEquality();
}

void RunClientIdTests() {
    std::printf("[clientid] two viewers on one machine do not answer to the same id...\n");
    std::set<uint32_t> ids;
    for (uint8_t sourceId = 0; sourceId < 8; ++sourceId)
        ids.insert(deskhubp::MakeClientId(sourceId));
    Check(ids.size() == 8,
        "one window per source, each with its own id, so the host can tell them apart");
}
