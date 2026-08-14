#include "deskhubp/net/QuicEndpoint.h"

#include "deskhubp/diag/Log.h"

namespace deskhubp {

namespace {

void WarnOnce() {
    static bool warned = false;
    if (warned) return;
    warned = true;
    LOGW("quic: this build has no QUIC library - run 'make bootstrap' to build quiche");
}

}

struct QuicEndpoint::Impl {
};

QuicEndpoint::QuicEndpoint() : impl_(std::make_unique<Impl>()) {
}

QuicEndpoint::~QuicEndpoint() = default;

bool QuicEndpoint::Listen(const QuicSettings&, const std::string&, uint16_t, QuicCallbacks) {
    WarnOnce();
    return false;
}

bool QuicEndpoint::Connect(const QuicSettings&, const NetAddr&, std::string_view, QuicCallbacks) {
    WarnOnce();
    return false;
}

void QuicEndpoint::Poll(uint64_t, uint32_t) {
}

bool QuicEndpoint::SendStream(QuicConnId, uint64_t, std::span<const uint8_t>, bool) {
    return false;
}

bool QuicEndpoint::SendDatagram(QuicConnId, std::span<const uint8_t>) {
    return false;
}

bool QuicEndpoint::SendRaw(const NetAddr&, std::span<const uint8_t>) {
    return false;
}

size_t QuicEndpoint::MaxDatagramSize(QuicConnId) const {
    return 0;
}

std::optional<deskhub::Fingerprint> QuicEndpoint::PeerFingerprint(QuicConnId) const {
    return std::nullopt;
}

bool QuicEndpoint::Established(QuicConnId) const {
    return false;
}

void QuicEndpoint::CloseConnection(QuicConnId, uint64_t, std::string_view) {
}

void QuicEndpoint::Close() {
}

bool QuicEndpoint::IsOpen() const {
    return false;
}

bool QuicEndpoint::IsServer() const {
    return false;
}

bool QuicEndpoint::LastBindAddrInUse() const {
    return false;
}

size_t QuicEndpoint::ConnectionCount() const {
    return 0;
}

QuicConnId QuicEndpoint::FirstConnection() const {
    return 0;
}

uint16_t QuicEndpoint::LocalPort() const {
    return 0;
}

std::vector<QuicConnId> QuicEndpoint::Connections() const {
    return {};
}

}
