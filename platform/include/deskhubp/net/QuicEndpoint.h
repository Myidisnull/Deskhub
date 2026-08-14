#pragma once
#include "deskhub/net/TrustStore.h"
#include "deskhubp/net/UdpSocket.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace deskhubp {

inline constexpr size_t kQuicMaxUdpPayload = 1350;
inline constexpr uint64_t kQuicIdleTimeoutMs = 30'000;
inline constexpr uint64_t kQuicControlStream = 0;
inline constexpr uint64_t kQuicFirstTerminalStream = 4;
inline constexpr uint64_t kQuicStreamStride = 4;

using QuicConnId = uint64_t;

struct QuicSettings {
    std::string alpn = "deskhub";
    std::string certPemPath{};
    std::string keyPemPath{};
    uint64_t idleTimeoutMs = kQuicIdleTimeoutMs;
    size_t maxUdpPayload = kQuicMaxUdpPayload;
    bool verifyPeer = false;
};

struct QuicCallbacks {
    std::function<void(QuicConnId, const NetAddr&)> onConnected;
    std::function<void(QuicConnId, const NetAddr&)> onClosed;
    std::function<void(QuicConnId, uint64_t streamId, std::span<const uint8_t> bytes, bool fin)>
        onStream;
    std::function<void(QuicConnId, std::span<const uint8_t> bytes)> onDatagram;
    std::function<void(const NetAddr& from, std::span<const uint8_t> bytes)> onForeignDatagram;
};

class QuicEndpoint {
public:
    QuicEndpoint();
    ~QuicEndpoint();
    QuicEndpoint(const QuicEndpoint&) = delete;
    QuicEndpoint& operator=(const QuicEndpoint&) = delete;

    bool Listen(const QuicSettings& settings, const std::string& bindIp, uint16_t port,
        QuicCallbacks callbacks);
    bool Connect(const QuicSettings& settings, const NetAddr& server,
        std::string_view serverName, QuicCallbacks callbacks);

    void Poll(uint64_t nowUs, uint32_t waitMs);

    bool SendStream(QuicConnId conn, uint64_t streamId, std::span<const uint8_t> bytes,
        bool fin = false);
    bool SendDatagram(QuicConnId conn, std::span<const uint8_t> bytes);
    bool SendRaw(const NetAddr& to, std::span<const uint8_t> bytes);

    size_t MaxDatagramSize(QuicConnId conn) const;
    std::optional<deskhub::Fingerprint> PeerFingerprint(QuicConnId conn) const;
    bool Established(QuicConnId conn) const;
    void CloseConnection(QuicConnId conn, uint64_t errorCode = 0, std::string_view reason = {});
    void Close();

    bool IsOpen() const;
    bool IsServer() const;
    bool LastBindAddrInUse() const;
    size_t ConnectionCount() const;
    QuicConnId FirstConnection() const;
    uint16_t LocalPort() const;
    std::vector<QuicConnId> Connections() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
