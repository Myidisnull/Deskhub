#include "deskhubp/host/HostPairingBroker.h"

#include "deskhub/net/PairedDevices.h"
#include "deskhubp/session/PairingAskQueue.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/system/PairedDevicesFile.h"
#include "deskhubp/system/UiSettingsStore.h"

namespace deskhubp {

void HostPairingBroker::Reset() {
    const std::lock_guard<std::mutex> lock(mutex_);
    authenticated_.clear();
    pending_.clear();
}

void HostPairingBroker::ForgetPeer(uint64_t addrPacked) {
    const std::lock_guard<std::mutex> lock(mutex_);
    authenticated_.erase(addrPacked);
    pending_.erase(addrPacked);
}

bool HostPairingBroker::IsAuthenticated(uint64_t addrPacked) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return authenticated_.contains(addrPacked);
}

void HostPairingBroker::SendResult(UdpSocket& sock, const NetAddr& to,
    deskhub::PairingResultCode code) {
    uint8_t buf[deskhub::kMaxDatagram];
    const deskhub::PairingResult result{code};
    const size_t n = deskhub::BuildPairingResult(buf, result);
    if (n) sock.SendTo(to, buf, n);
}

void HostPairingBroker::AcceptPeer(UdpSocket& sock, const NetAddr& peer,
    const deskhub::Fingerprint& fp, std::string_view name) {
    const uint64_t packed = peer.Pack();
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        authenticated_.insert(packed);
        pending_.erase(packed);
    }
    RememberPairedDevice(fp, name, NowUnixSeconds());
    SendResult(sock, peer, deskhub::PairingResultCode::Accepted);
}

void HostPairingBroker::HandleDatagram(UdpSocket& sock, const NetAddr& from,
    std::span<const uint8_t> datagram) {
    const std::optional<deskhub::CommonHeader> header = deskhub::ParseCommonHeader(datagram);
    if (!header || header->chan != deskhub::Chan::Control ||
        header->type != deskhub::MsgType::PairingHello)
        return;

    const std::optional<deskhub::PairingHello> hello =
        deskhub::ParsePairingHello(deskhub::PayloadOf(datagram));
    if (!hello) return;

    const uint64_t packed = from.Pack();
    if (CheckPairedDevice(hello->fingerprint) == deskhub::PairVerdict::Paired) {
        TouchPairedDevice(hello->fingerprint, hello->clientName, NowUnixSeconds());
        AcceptPeer(sock, from, hello->fingerprint, hello->clientName);
        return;
    }

    if (!LoadUiSettings().allowNewPairings) {
        SendResult(sock, from, deskhub::PairingResultCode::Disabled);
        return;
    }

    bool queueAsk = false;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (authenticated_.contains(packed)) {
            SendResult(sock, from, deskhub::PairingResultCode::Accepted);
            return;
        }
        const auto at = pending_.find(packed);
        if (at == pending_.end()) {
            pending_.emplace(packed, PendingPairing{hello->fingerprint, hello->clientName});
            queueAsk = true;
        } else {
            at->second.fingerprint = hello->fingerprint;
            at->second.name = hello->clientName;
        }
    }

    if (queueAsk) {
        PairingAsk ask;
        ask.addrPacked = packed;
        ask.shortKey = deskhub::ShortFingerprint(hello->fingerprint);
        ask.name = hello->clientName;
        SharedPairingAskQueue().Push(std::move(ask));
    }
}

void HostPairingBroker::DrainAnswers(UdpSocket& sock) {
    std::vector<std::pair<uint64_t, bool>> answers;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [addrPacked, pending] : pending_) {
            (void)pending;
            bool allowed = false;
            if (SharedPairingAskQueue().TakeAnswer(addrPacked, allowed))
                answers.emplace_back(addrPacked, allowed);
        }
    }

    for (const auto& [addrPacked, allowed] : answers) {
        const NetAddr peer = NetAddr::Unpack(addrPacked);
        PendingPairing pending;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            const auto at = pending_.find(addrPacked);
            if (at == pending_.end()) continue;
            pending = at->second;
            pending_.erase(at);
        }

        if (!allowed) {
            SendResult(sock, peer, deskhub::PairingResultCode::Refused);
            continue;
        }

        AcceptPeer(sock, peer, pending.fingerprint, pending.name);
    }
}

}
