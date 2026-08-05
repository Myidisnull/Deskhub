#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/net/LanScan.h"
#include "deskhubp/net/LanScanner.h"
#include "deskhubp/net/NetInfo.h"
#include "deskhubp/net/UdpSocket.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <vector>

namespace {

void TestEveryAdapterReportsAScannablePrefix() {
    std::printf("[scan] each local address comes with a usable subnet size...\n");
    for (const AdapterAddr& adapter : ListLocalIPv4()) {
        Check(adapter.prefixLen >= 1 && adapter.prefixLen <= 32,
            "a prefix length outside 1..32 would make the sweep meaningless");
        NetAddr parsed{};
        Check(ParseNetAddr(adapter.ip, parsed), "an adapter address is always parseable");
    }
}

void TestTheSweepNeverProbesThisMachine() {
    std::printf("[scan] the sweep list holds neighbours only, each one once...\n");
    const std::vector<uint32_t> targets = deskhubp::LocalScanTargets();

    std::set<uint32_t> own;
    for (const AdapterAddr& adapter : ListLocalIPv4()) {
        NetAddr parsed{};
        if (ParseNetAddr(adapter.ip, parsed)) own.insert(parsed.ip);
    }

    for (uint32_t ip : targets)
        Check(!own.count(ip), "this machine's own address is never probed");

    const std::set<uint32_t> unique(targets.begin(), targets.end());
    Check(unique.size() == targets.size(),
        "overlapping adapters do not make us probe the same host twice");
    Check(targets.size() <= deskhub::kMaxScanTargets, "the sweep stays within its budget");
}

void TestVirtualAdaptersAreLeftAlone() {
    std::printf("[scan] virtual switches and VM adapters are not swept...\n");
    std::set<uint32_t> reachable;
    for (const AdapterAddr& adapter : ListLocalIPv4()) {
        if (adapter.virtualAdapter) continue;
        NetAddr parsed{};
        if (!ParseNetAddr(adapter.ip, parsed)) continue;
        for (uint32_t ip : deskhub::SubnetScanTargets(parsed.ip, adapter.prefixLen))
            reachable.insert(ip);
    }

    for (uint32_t ip : deskhubp::LocalScanTargets())
        Check(reachable.count(ip) > 0,
            "every probed address belongs to a real network this machine is on");
}

}

void RunLanScannerTests() {
    TestEveryAdapterReportsAScannablePrefix();
    TestTheSweepNeverProbesThisMachine();
    TestVirtualAdaptersAreLeftAlone();
}
