#pragma once
#include "deskhub/net/TrustStore.h"
#include "deskhub/protocol/RecordStream.h"
#include "deskhubp/net/QuicEndpoint.h"
#include "deskhubp/session/AuthNegotiation.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace deskhubp {

enum class VideoPath : uint8_t {
    RawUdp = 0,
    QuicDatagram = 1,
};

struct TransportMessage {
    NetAddr from{};
    std::vector<uint8_t> bytes{};
};

struct TransportAuthCallbacks {
    std::function<void(const NetAddr&, const deskhub::Fingerprint&, std::string_view name)>
        onPaired;
    std::function<void(const NetAddr&, const deskhub::Fingerprint&, std::string_view name)>
        onApprovalNeeded;
    std::function<void(const NetAddr&, deskhub::AuthResultCode)> onRefused;
};

class SessionTransport {
public:
    SessionTransport();
    ~SessionTransport();
    SessionTransport(const SessionTransport&) = delete;
    SessionTransport& operator=(const SessionTransport&) = delete;

    bool Listen(const QuicSettings& settings, uint16_t port, const std::string& bindIp = {});
    bool Connect(const QuicSettings& settings, const NetAddr& server,
        std::string_view serverName);
    bool WaitEstablished(const NetAddr& peer, uint32_t timeoutMs);
    void Close();

    bool SetRecvTimeout(uint32_t ms);
    bool SendTo(const NetAddr& to, const uint8_t* data, size_t len);
    int RecvFrom(uint8_t* buf, size_t cap, NetAddr& from);

    // A machine that has not proved itself gets nothing through: until its auth
    // settles, everything else it sends is dropped rather than handed upwards.
    void SetHostAuth(HostAuthConfig config, TransportAuthCallbacks callbacks);
    bool RunClientAuth(const NetAddr& server, ClientAuthConfig config, uint32_t timeoutMs,
        deskhub::AuthResultCode& outCode, bool& outHostProvedPasscode);
    void ApproveConnection(const NetAddr& peer, bool allowed);
    bool Authenticated(const NetAddr& peer) const;

    void SetVideoPath(VideoPath path);
    VideoPath videoPath() const;

    void SetOnPeerGone(std::function<void(const NetAddr&)> fn);

    std::optional<deskhub::Fingerprint> PeerFingerprint(const NetAddr& peer) const;
    bool Established(const NetAddr& peer) const;
    size_t MaxDatagramSize(const NetAddr& peer) const;

    bool IsOpen() const;
    bool lastBindAddrInUse() const;
    uint16_t LocalPort() const;

private:
    QuicCallbacks MakeCallbacks();
    void OnStream(QuicConnId conn, uint64_t stream, std::span<const uint8_t> bytes);
    void Deliver(const NetAddr& from, std::span<const uint8_t> message, bool overQuic);
    bool SendReliable(const NetAddr& to, std::span<const uint8_t> message);
    bool HandleHostAuth(const NetAddr& from, std::span<const uint8_t> message);
    void SendAuth(const NetAddr& to, std::span<const uint8_t> message);
    void SettleHostAuth(const NetAddr& peer, HostAuth& auth, const deskhub::AuthResult& result);

    QuicEndpoint endpoint_;
    std::map<uint64_t, deskhub::RecordStream> framers_;
    std::deque<TransportMessage> inbox_;
    std::deque<TransportMessage> authInbox_;
    std::map<uint64_t, std::unique_ptr<HostAuth>> hostAuth_;
    std::map<uint64_t, bool> authenticated_;
    HostAuthConfig hostAuthConfig_{};
    TransportAuthCallbacks authCallbacks_{};
    bool hostAuthOn_ = false;
    bool clientAuthOn_ = false;
    std::function<void(const NetAddr&)> onPeerGone_;
    VideoPath videoPath_ = VideoPath::RawUdp;
    uint32_t recvWaitMs_ = 10;
    mutable std::mutex sendMutex_;
};

}
