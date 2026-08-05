#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/net/LanScan.h"
#include "deskhub/protocol/Wire.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace deskhub;

namespace {

constexpr uint32_t kHomeIp = 0xC0A80105u;

bool Holds(const std::vector<uint32_t>& targets, uint32_t ip) {
    return std::find(targets.begin(), targets.end(), ip) != targets.end();
}

void TestOrdinaryHomeNetwork() {
    std::printf("[scan] a /24 yields every neighbour, and nothing that is not one...\n");
    const auto targets = SubnetScanTargets(kHomeIp, 24);
    Check(targets.size() == 253, "254 usable addresses minus this machine itself");
    Check(targets.front() == 0xC0A80101u, "the sweep starts one past the network address");
    Check(targets.back() == 0xC0A801FEu, "the sweep stops one short of the broadcast address");
    Check(!Holds(targets, kHomeIp), "this machine is not offered as something to connect to");
    Check(!Holds(targets, 0xC0A80100u), "the network address is never probed");
    Check(!Holds(targets, 0xC0A801FFu), "the broadcast address is never probed");
    Check(!Holds(targets, 0xC0A80205u), "a neighbouring subnet is left alone");
}

void TestSmallSubnets() {
    std::printf("[scan] tiny subnets stay inside their own range...\n");
    const auto four = SubnetScanTargets(0x0A000001u, 30);
    Check(four.size() == 1, "a /30 has two usable addresses, one of which is us");
    Check(four.front() == 0x0A000002u, "the only neighbour of 10.0.0.1/30 is 10.0.0.2");

    Check(SubnetScanTargets(0xC0A80100u, 24).empty(),
        "an address that is really a network address gives nothing to scan");
    Check(SubnetScanTargets(0xC0A801FFu, 24).empty(),
        "a broadcast address gives nothing to scan either");
    Check(SubnetScanTargets(0x0A000001u, 31).empty(), "a /31 has no host to scan");
    Check(SubnetScanTargets(0x0A000001u, 32).empty(), "a /32 has no host to scan");
    Check(SubnetScanTargets(0, 24).empty(), "without a local address there is nothing to sweep");
    Check(SubnetScanTargets(kHomeIp, 24, 0).empty(), "a zero budget probes nothing");
}

void TestWideSubnetsAreCappedAroundUs() {
    std::printf("[scan] a huge subnet is narrowed to a window around this machine...\n");
    const auto targets = SubnetScanTargets(kHomeIp, 16, 64);
    Check(targets.size() == 63, "the window holds the budget, minus this machine");
    Check(targets.front() == kHomeIp - 32 && targets.back() == kHomeIp + 31,
        "the window sits around this machine, not at the far end of the /16");
    Check(!Holds(targets, kHomeIp), "this machine is still skipped inside the window");

    const auto atEdge = SubnetScanTargets(0xC0A80001u, 16, 64);
    Check(atEdge.size() == 63, "a machine near the bottom of the range still gets a full window");
    Check(atEdge.front() == 0xC0A80002u, "the window cannot start before the first host");

    const auto atTop = SubnetScanTargets(0xC0A8FFFEu, 16, 64);
    Check(atTop.size() == 63, "a machine near the top of the range still gets a full window");
    Check(atTop.back() == 0xC0A8FFFDu, "the window cannot run past the last host");

    const auto whole = SubnetScanTargets(0x0A000001u, 0, 32);
    Check(whole.size() == 31, "even a /0 is reduced to something a sweep can finish");
}

void TestAddressText() {
    std::printf("[scan] a found host is written the way the user would type it...\n");
    Check(FormatIPv4(kHomeIp) == "192.168.1.5", "dotted quad, high byte first");
    Check(FormatIPv4(0) == "0.0.0.0", "an empty address still formats");
    Check(FormatIPv4(0xFFFFFFFFu) == "255.255.255.255", "the top address formats");
    Check(ScanAddressText(kHomeIp, kDeskhubPort) == "192.168.1.5",
        "the default port is left out, as the address box expects");
    Check(ScanAddressText(kHomeIp, 0) == "192.168.1.5", "no port at all is the default port");
    Check(ScanAddressText(kHomeIp, 5000) == "192.168.1.5:5000",
        "an unusual port is kept so reconnecting lands on the same host");
}

}

void RunLanScanTests() {
    TestOrdinaryHomeNetwork();
    TestSmallSubnets();
    TestWideSubnetsAreCappedAroundUs();
    TestAddressText();
}
