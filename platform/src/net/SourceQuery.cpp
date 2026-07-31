#include "deskhubp/net/SourceQuery.h"

#include <cinttypes>

#include "deskhubp/system/Clock.h"
#include "deskhubp/diag/Log.h"

namespace {
constexpr uint64_t kQueryTimeoutUs = 3'000'000;
constexpr uint64_t kResendUs = 500'000;
}

bool QuerySources(const NetAddr& server, std::vector<deskhub::SourceInfo>& out) {
    out.clear();

    UdpSocket sock;
    if (!sock.Open(0)) {
        LOGE("[Sources] Failed to open socket.");
        return false;
    }
    sock.SetRecvTimeout(200);

    uint8_t query[deskhub::kMaxDatagram];
    const size_t qn = deskhub::BuildListSources(query);
    if (!qn) return false;

    uint8_t buf[deskhub::kMaxDatagram];
    const uint64_t startUs = NowUs();
    uint64_t lastSendUs = 0;
    while (NowUs() - startUs < kQueryTimeoutUs) {
        const uint64_t now = NowUs();
        if (now - lastSendUs >= kResendUs) {
            lastSendUs = now;
            sock.SendTo(server, query, qn);
        }

        NetAddr from;
        const int n = sock.RecvFrom(buf, sizeof(buf), from);
        if (n == 0) continue;
        if (n < 0) {
            LOGE("[Sources] Socket error while waiting for SOURCE_LIST.");
            return false;
        }

        if (!(from == server)) continue;
        const auto span = std::span<const uint8_t>(buf, size_t(n));
        const auto h = deskhub::ParseCommonHeader(span);
        if (!h || h->type != deskhub::MsgType::SourceList) continue;

        deskhub::SourceInfo tmp[deskhub::kMaxSources];
        const size_t cnt = deskhub::ParseSourceList(deskhub::PayloadOf(span), tmp);
        for (size_t i = 0; i < cnt; ++i) out.push_back(std::move(tmp[i]));
        LOGI("[Sources] Host is sharing %zu source(s).", out.size());
        return true;
    }

    LOGW("[Sources] No SOURCE_LIST from %s after %" PRIu64 " ms.", server.ToString().c_str(),
        kQueryTimeoutUs / 1000);
    return false;
}
