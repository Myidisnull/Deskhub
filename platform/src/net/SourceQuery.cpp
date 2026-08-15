#include "deskhubp/net/SourceQuery.h"

#include <cinttypes>

#include "deskhubp/diag/Log.h"
#include "deskhubp/net/SessionTransport.h"
#include "deskhubp/system/UiSettingsStore.h"
#include "deskhubp/system/HostIdentity.h"
#include "deskhubp/system/Clock.h"

namespace {
constexpr uint32_t kHandshakeTimeoutMs = 5'000;
constexpr uint32_t kAuthTimeoutMs = 65'000;
constexpr uint64_t kListTimeoutUs = 5'000'000;
constexpr uint32_t kPollWaitMs = 2;
}

// What a machine is sharing is no longer told to anyone who asks: the list now comes
// back only inside an encrypted connection this machine has proved itself on. The
// beacon still answers strangers, but with nothing in it.
bool QuerySources(const NetAddr& server, std::vector<deskhub::SourceInfo>& out,
    const std::string& passcode, deskhub::AuthResultCode* outCode) {
    out.clear();
    if (outCode) *outCode = deskhub::AuthResultCode::NotPaired;

    if (!deskhubp::QuicAvailable()) {
        LOGE("[Sources] This build has no QUIC library.");
        return false;
    }

    deskhubp::SessionTransport transport;
    transport.SetRecvTimeout(kPollWaitMs);
    if (!transport.Connect(deskhubp::QuicSettings{}, server, server.ToString())) {
        LOGE("[Sources] Could not open a connection to %s.", server.ToString().c_str());
        return false;
    }
    if (!transport.WaitEstablished(server, kHandshakeTimeoutMs)) {
        LOGW("[Sources] %s did not answer the handshake.", server.ToString().c_str());
        return false;
    }

    const std::string name = deskhubp::SessionDeviceName();
    deskhubp::ClientAuthConfig auth;
    auth.identity = deskhubp::LoadOrCreateHostIdentity(name);
    auth.passcode = passcode;
    auth.clientName = name;
    if (const std::optional<deskhub::Fingerprint> peer = transport.PeerFingerprint(server))
        auth.hostFingerprint = *peer;

    deskhub::AuthResultCode code = deskhub::AuthResultCode::NotPaired;
    bool hostProved = false;
    const bool allowed =
        transport.RunClientAuth(server, std::move(auth), kAuthTimeoutMs, code, hostProved);
    if (outCode) *outCode = code;
    if (!allowed) {
        LOGW("[Sources] %s did not let this machine in.", server.ToString().c_str());
        return false;
    }

    uint8_t query[deskhub::kMaxDatagram];
    const size_t qn = deskhub::BuildListSources(query);
    if (!qn) return false;
    transport.SendTo(server, query, qn);

    uint8_t buf[deskhub::kMaxDatagram];
    const uint64_t deadline = NowUs() + kListTimeoutUs;
    while (NowUs() < deadline) {
        NetAddr from;
        const int n = transport.RecvFrom(buf, sizeof(buf), from);
        if (n <= 0) continue;
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
        kListTimeoutUs / 1000);
    return false;
}
