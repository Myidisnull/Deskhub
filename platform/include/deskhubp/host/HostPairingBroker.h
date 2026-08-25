#pragma once
#include "deskhub/protocol/Wire.h"
#include "deskhubp/net/UdpSocket.h"

#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace deskhubp {

class HostPairingBroker {
public:
    void Reset();
    void HandleDatagram(UdpSocket& sock, const NetAddr& from, std::span<const uint8_t> datagram);
    void DrainAnswers(UdpSocket& sock);
    void ForgetPeer(uint64_t addrPacked);
    bool IsAuthenticated(uint64_t addrPacked) const;

private:
    struct PendingPairing {
        deskhub::Fingerprint fingerprint{};
        std::string name{};
    };

    void SendResult(UdpSocket& sock, const NetAddr& to, deskhub::PairingResultCode code);
    void AcceptPeer(UdpSocket& sock, const NetAddr& peer, const deskhub::Fingerprint& fp,
        std::string_view name);

    mutable std::mutex mutex_;
    std::unordered_set<uint64_t> authenticated_;
    std::unordered_map<uint64_t, PendingPairing> pending_;
};

}
