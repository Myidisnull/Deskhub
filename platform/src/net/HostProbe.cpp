#include "deskhubp/net/HostProbe.h"

#include <algorithm>

#include "deskhub/protocol/Wire.h"
#include "deskhubp/system/Clock.h"

namespace deskhubp {

namespace {
constexpr uint64_t kResendUs = 250'000;
constexpr uint32_t kRecvSliceMs = 50;
}

std::vector<std::optional<uint32_t>> ProbeHostsRttMs(std::span<const NetAddr> hosts,
    uint32_t timeoutMs) {
    std::vector<std::optional<uint32_t>> out(hosts.size());
    if (hosts.empty()) return out;

    UdpSocket sock;
    if (!sock.Open(0)) return out;
    sock.SetRecvTimeout(kRecvSliceMs);

    uint8_t query[deskhub::kMaxDatagram];
    const size_t queryLen = deskhub::BuildListSources(query);
    if (!queryLen) return out;

    std::vector<uint64_t> lastSendUs(hosts.size(), 0);
    size_t remaining = hosts.size();
    const uint64_t startUs = NowUs();
    const uint64_t budgetUs = uint64_t(timeoutMs) * 1000;

    while (remaining && NowUs() - startUs < budgetUs) {
        const uint64_t now = NowUs();
        for (size_t i = 0; i < hosts.size(); ++i) {
            if (out[i]) continue;
            if (lastSendUs[i] && now - lastSendUs[i] < kResendUs) continue;
            lastSendUs[i] = now;
            sock.SendTo(hosts[i], query, queryLen);
        }

        uint8_t buf[deskhub::kMaxDatagram];
        NetAddr from;
        const int n = sock.RecvFrom(buf, sizeof(buf), from);
        if (n < 0) break;
        if (n == 0) continue;

        const auto header = deskhub::ParseCommonHeader(std::span<const uint8_t>(buf, size_t(n)));
        if (!header || header->type != deskhub::MsgType::SourceList) continue;

        for (size_t i = 0; i < hosts.size(); ++i) {
            if (out[i] || !(hosts[i] == from)) continue;
            const uint64_t rttUs = NowUs() - lastSendUs[i];
            out[i] = std::max(uint32_t{1}, uint32_t(rttUs / 1000));
            --remaining;
            break;
        }
    }
    return out;
}

std::optional<uint32_t> ProbeHostRttMs(const NetAddr& host, uint32_t timeoutMs) {
    const NetAddr hosts[1] = {host};
    return ProbeHostsRttMs(hosts, timeoutMs)[0];
}

}
