#include "deskhubp/net/QuicEndpoint.h"

#include <quiche.h>

#include <algorithm>
#include <cstring>
#include <unordered_map>

#include "deskhub/protocol/Wire.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/system/HostIdentity.h"
#include "deskhubp/system/Random.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace deskhubp {

namespace {

constexpr size_t kConnIdLen = 16;
constexpr size_t kStreamChunk = 8192;
constexpr uint64_t kInitialMaxData = 4u << 20;
constexpr uint64_t kInitialMaxStreamData = 1u << 20;
constexpr uint64_t kMaxStreams = 64;
constexpr uint64_t kDropLogIntervalUs = 1'000'000;
constexpr size_t kDatagramQueue = 512;

sockaddr_in ToSockAddr(const NetAddr& addr) {
    sockaddr_in out{};
    out.sin_family = AF_INET;
    out.sin_port = htons(addr.port);
    out.sin_addr.s_addr = htonl(addr.ip);
    return out;
}

quiche_config* MakeConfig(const QuicSettings& settings, bool server) {
    quiche_config* cfg = quiche_config_new(QUICHE_PROTOCOL_VERSION);
    if (cfg == nullptr) return nullptr;

    if (server) {
        if (quiche_config_load_cert_chain_from_pem_file(cfg, settings.certPemPath.c_str()) < 0 ||
            quiche_config_load_priv_key_from_pem_file(cfg, settings.keyPemPath.c_str()) < 0) {
            LOGE("quic: could not load the host certificate or key");
            quiche_config_free(cfg);
            return nullptr;
        }
    }
    quiche_config_verify_peer(cfg, settings.verifyPeer);

    std::vector<uint8_t> alpn;
    alpn.push_back(uint8_t(settings.alpn.size()));
    alpn.insert(alpn.end(), settings.alpn.begin(), settings.alpn.end());
    quiche_config_set_application_protos(cfg, alpn.data(), alpn.size());

    quiche_config_set_max_idle_timeout(cfg, settings.idleTimeoutMs);
    quiche_config_set_max_recv_udp_payload_size(cfg, settings.maxUdpPayload);
    quiche_config_set_max_send_udp_payload_size(cfg, settings.maxUdpPayload);
    quiche_config_set_initial_max_data(cfg, kInitialMaxData);
    quiche_config_set_initial_max_stream_data_bidi_local(cfg, kInitialMaxStreamData);
    quiche_config_set_initial_max_stream_data_bidi_remote(cfg, kInitialMaxStreamData);
    quiche_config_set_initial_max_stream_data_uni(cfg, kInitialMaxStreamData);
    quiche_config_set_initial_max_streams_bidi(cfg, kMaxStreams);
    quiche_config_set_initial_max_streams_uni(cfg, kMaxStreams);
    quiche_config_enable_dgram(cfg, true, kDatagramQueue, kDatagramQueue);
    return cfg;
}

void FillRandomConnId(uint8_t* out, size_t len) {
    if (RandomBytes(out, len)) return;
    for (size_t i = 0; i < len; ++i) out[i] = uint8_t(i * 31 + 7);
}

}

struct QuicEndpoint::Impl {
    struct Connection {
        quiche_conn* conn = nullptr;
        NetAddr peer{};
        bool announced = false;
    };

    ~Impl() {
        Shutdown();
    }

    void Shutdown() {
        for (auto& [key, entry] : connections_)
            if (entry.conn != nullptr) quiche_conn_free(entry.conn);
        connections_.clear();
        socket_.Close();
        if (config_ != nullptr) {
            quiche_config_free(config_);
            config_ = nullptr;
        }
        open_ = false;
    }

    bool Start(const QuicSettings& settings, const std::string& bindIp, uint16_t port,
        bool server, QuicCallbacks callbacks) {
        Shutdown();
        settings_ = settings;
        cb_ = std::move(callbacks);
        server_ = server;
        config_ = MakeConfig(settings_, server);
        if (config_ == nullptr) return false;
        if (!socket_.Open(port, bindIp)) {
            bindAddrInUse_ = socket_.lastBindAddrInUse();
            LOGE("quic: could not bind UDP port %u%s", unsigned(port),
                bindAddrInUse_ ? " - something else is already listening on it" : "");
            quiche_config_free(config_);
            config_ = nullptr;
            return false;
        }
        socket_.SetRecvTimeout(1);
        localPort_ = socket_.LocalPort();
        localIp_ = bindIp;
        open_ = true;
        return true;
    }

    bool Dial(const NetAddr& server, std::string_view serverName) {
        const sockaddr_in local = LocalSockAddr();
        const sockaddr_in peer = ToSockAddr(server);
        uint8_t scid[kConnIdLen];
        FillRandomConnId(scid, sizeof(scid));

        const std::string name(serverName);
        quiche_conn* conn = quiche_connect(name.empty() ? nullptr : name.c_str(), scid,
            sizeof(scid), reinterpret_cast<const sockaddr*>(&local), sizeof(local),
            reinterpret_cast<const sockaddr*>(&peer), sizeof(peer), config_);
        if (conn == nullptr) {
            LOGE("quic: could not start a connection to %s", server.ToString().c_str());
            return false;
        }
        Connection entry;
        entry.conn = conn;
        entry.peer = server;
        connections_.emplace(server.Pack(), entry);
        Flush(connections_[server.Pack()]);
        return true;
    }

    sockaddr_in LocalSockAddr() const {
        NetAddr local{0, localPort_};
        if (!localIp_.empty()) ParseNetAddr(localIp_ + ":0", local);
        local.port = localPort_;
        return ToSockAddr(local);
    }

    Connection* Lookup(QuicConnId id) {
        const auto at = connections_.find(id);
        return at == connections_.end() ? nullptr : &at->second;
    }

    const Connection* Lookup(QuicConnId id) const {
        const auto at = connections_.find(id);
        return at == connections_.end() ? nullptr : &at->second;
    }

    void Flush(Connection& entry) {
        uint8_t out[kQuicMaxUdpPayload];
        for (;;) {
            quiche_send_info info{};
            const ssize_t written = quiche_conn_send(entry.conn, out, sizeof(out), &info);
            if (written == QUICHE_ERR_DONE) return;
            if (written < 0) {
                LOGW("quic: send failed (%zd)", written);
                return;
            }
            socket_.SendTo(entry.peer, out, size_t(written));
        }
    }

    void Accept(const NetAddr& from, std::span<const uint8_t> packet) {
        uint32_t version = 0;
        uint8_t type = 0;
        uint8_t scid[QUICHE_MAX_CONN_ID_LEN];
        uint8_t dcid[QUICHE_MAX_CONN_ID_LEN];
        uint8_t token[256];
        size_t scidLen = sizeof(scid);
        size_t dcidLen = sizeof(dcid);
        size_t tokenLen = sizeof(token);
        if (quiche_header_info(packet.data(), packet.size(), kConnIdLen, &version, &type, scid,
                &scidLen, dcid, &dcidLen, token, &tokenLen) < 0)
            return;

        const sockaddr_in local = LocalSockAddr();
        const sockaddr_in peer = ToSockAddr(from);
        uint8_t ourScid[kConnIdLen];
        FillRandomConnId(ourScid, sizeof(ourScid));
        quiche_conn* conn = quiche_accept(ourScid, sizeof(ourScid), nullptr, 0,
            reinterpret_cast<const sockaddr*>(&local), sizeof(local),
            reinterpret_cast<const sockaddr*>(&peer), sizeof(peer), config_);
        if (conn == nullptr) return;

        Connection entry;
        entry.conn = conn;
        entry.peer = from;
        connections_.emplace(from.Pack(), entry);
    }

    void Receive(const NetAddr& from, std::span<const uint8_t> packet) {
        if (deskhub::ClassifyPacket(packet) != deskhub::PacketKind::Quic) {
            if (cb_.onForeignDatagram) cb_.onForeignDatagram(from, packet);
            return;
        }

        Connection* entry = Lookup(from.Pack());
        if (entry == nullptr) {
            if (!server_) return;
            if (connections_.size() >= kMaxConnections) return;
            Accept(from, packet);
            entry = Lookup(from.Pack());
            if (entry == nullptr) return;
        }

        const sockaddr_in local = LocalSockAddr();
        const sockaddr_in peer = ToSockAddr(from);
        quiche_recv_info info{};
        info.from = reinterpret_cast<sockaddr*>(const_cast<sockaddr_in*>(&peer));
        info.from_len = sizeof(peer);
        info.to = reinterpret_cast<sockaddr*>(const_cast<sockaddr_in*>(&local));
        info.to_len = sizeof(local);

        std::vector<uint8_t> copy(packet.begin(), packet.end());
        const ssize_t read = quiche_conn_recv(entry->conn, copy.data(), copy.size(), &info);
        if (read < 0) ReportDrop(from, read);
    }

    void ReportDrop(const NetAddr& from, ssize_t code) {
        ++drops_;
        const uint64_t nowUs = NowUs();
        if (nowUs - lastDropLogUs_ < kDropLogIntervalUs) return;
        lastDropLogUs_ = nowUs;
        LOGW("quic: dropped %llu unusable packet(s), last from %s (%zd)",
            static_cast<unsigned long long>(drops_), from.ToString().c_str(), code);
        drops_ = 0;
    }

    void DrainStreams(QuicConnId id, Connection& entry) {
        quiche_stream_iter* it = quiche_conn_readable(entry.conn);
        if (it == nullptr) return;
        uint64_t streamId = 0;
        std::vector<uint64_t> ready;
        while (quiche_stream_iter_next(it, &streamId)) ready.push_back(streamId);
        quiche_stream_iter_free(it);

        std::vector<uint8_t> chunk(kStreamChunk);
        for (uint64_t stream : ready) {
            for (;;) {
                bool fin = false;
                uint64_t err = 0;
                const ssize_t got = quiche_conn_stream_recv(entry.conn, stream, chunk.data(),
                    chunk.size(), &fin, &err);
                if (got < 0) break;
                if (cb_.onStream)
                    cb_.onStream(id, stream, std::span<const uint8_t>(chunk.data(), size_t(got)),
                        fin);
                if (fin || size_t(got) < chunk.size()) break;
            }
        }
    }

    void DrainDatagrams(QuicConnId id, Connection& entry) {
        std::vector<uint8_t> chunk(settings_.maxUdpPayload);
        for (;;) {
            const ssize_t got = quiche_conn_dgram_recv(entry.conn, chunk.data(), chunk.size());
            if (got < 0) return;
            if (cb_.onDatagram)
                cb_.onDatagram(id, std::span<const uint8_t>(chunk.data(), size_t(got)));
        }
    }

    void Service() {
        std::vector<QuicConnId> dead;
        for (auto& [id, entry] : connections_) {
            if (quiche_conn_is_established(entry.conn) && !entry.announced) {
                entry.announced = true;
                if (cb_.onConnected) cb_.onConnected(id, entry.peer);
            }
            if (entry.announced) {
                DrainStreams(id, entry);
                DrainDatagrams(id, entry);
            }
            Flush(entry);
            if (quiche_conn_is_closed(entry.conn)) dead.push_back(id);
        }
        for (QuicConnId id : dead) {
            Connection& entry = connections_[id];
            if (entry.announced && cb_.onClosed) cb_.onClosed(id, entry.peer);
            quiche_conn_free(entry.conn);
            connections_.erase(id);
        }
    }

    bool WaitReadable(uint32_t waitMs) {
        return open_ && socket_.WaitReadable(waitMs);
    }

    void Poll(uint32_t waitMs) {
        if (!open_) return;
        socket_.SetRecvTimeout(waitMs == 0 ? 1 : waitMs);
        uint8_t buf[kQuicMaxUdpPayload];
        for (int i = 0; i < kPacketsPerPoll; ++i) {
            NetAddr from{};
            const int got = socket_.RecvFrom(buf, sizeof(buf), from);
            if (got <= 0) break;
            Receive(from, std::span<const uint8_t>(buf, size_t(got)));
        }
        for (auto& [id, entry] : connections_) {
            if (quiche_conn_timeout_as_millis(entry.conn) == 0) quiche_conn_on_timeout(entry.conn);
        }
        Service();
    }

    static constexpr size_t kMaxConnections = 32;
    static constexpr int kPacketsPerPoll = 256;

    UdpSocket socket_{};
    quiche_config* config_ = nullptr;
    QuicSettings settings_{};
    QuicCallbacks cb_{};
    std::unordered_map<QuicConnId, Connection> connections_{};
    uint16_t localPort_ = 0;
    std::string localIp_{};
    bool bindAddrInUse_ = false;
    bool server_ = false;
    bool open_ = false;
    uint64_t drops_ = 0;
    uint64_t lastDropLogUs_ = 0;
};

QuicEndpoint::QuicEndpoint() : impl_(std::make_unique<Impl>()) {
}

QuicEndpoint::~QuicEndpoint() = default;

bool QuicEndpoint::Listen(const QuicSettings& settings, const std::string& bindIp, uint16_t port,
    QuicCallbacks callbacks) {
    return impl_->Start(settings, bindIp, port, true, std::move(callbacks));
}

bool QuicEndpoint::Connect(const QuicSettings& settings, const NetAddr& server,
    std::string_view serverName, QuicCallbacks callbacks) {
    if (!impl_->Start(settings, {}, 0, false, std::move(callbacks))) return false;
    return impl_->Dial(server, serverName);
}

void QuicEndpoint::Poll(uint64_t, uint32_t waitMs) {
    impl_->Poll(waitMs);
}

bool QuicEndpoint::WaitReadable(uint32_t waitMs) {
    return impl_->WaitReadable(waitMs);
}

bool QuicEndpoint::SendStream(QuicConnId conn, uint64_t streamId, std::span<const uint8_t> bytes,
    bool fin) {
    Impl::Connection* entry = impl_->Lookup(conn);
    if (entry == nullptr || !quiche_conn_is_established(entry->conn)) return false;
    if (bytes.empty()) return true;
    const ssize_t room = quiche_conn_stream_capacity(entry->conn, streamId);
    if (room >= 0 && size_t(room) < bytes.size()) return false;
    size_t at = 0;
    while (at < bytes.size()) {
        uint64_t err = 0;
        const ssize_t written = quiche_conn_stream_send(entry->conn, streamId,
            bytes.data() + at, bytes.size() - at, fin && at + 1 >= bytes.size(), &err);
        if (written <= 0) break;
        at += size_t(written);
    }
    impl_->Flush(*entry);
    if (at == bytes.size()) return true;
    LOGW(
        "quic: stream %llu took only %zu of %zu bytes; half a record on the wire desyncs the "
        "framing on the far side, so the connection goes instead",
        (unsigned long long)(streamId), at, bytes.size());
    CloseConnection(conn, 1, "record truncated");
    return false;
}

bool QuicEndpoint::SendDatagram(QuicConnId conn, std::span<const uint8_t> bytes) {
    Impl::Connection* entry = impl_->Lookup(conn);
    if (entry == nullptr || !quiche_conn_is_established(entry->conn)) return false;
    const ssize_t sent = quiche_conn_dgram_send(entry->conn, bytes.data(), bytes.size());
    impl_->Flush(*entry);
    return sent > 0;
}

bool QuicEndpoint::SendKeepalive(QuicConnId conn) {
    Impl::Connection* entry = impl_->Lookup(conn);
    if (entry == nullptr || !quiche_conn_is_established(entry->conn)) return false;
    const ssize_t queued = quiche_conn_send_ack_eliciting(entry->conn);
    impl_->Flush(*entry);
    return queued >= 0;
}

bool QuicEndpoint::SendRaw(const NetAddr& to, std::span<const uint8_t> bytes) {
    return impl_->socket_.SendTo(to, bytes.data(), bytes.size());
}

size_t QuicEndpoint::MaxDatagramSize(QuicConnId conn) const {
    const Impl::Connection* entry = impl_->Lookup(conn);
    if (entry == nullptr) return 0;
    const ssize_t max = quiche_conn_dgram_max_writable_len(entry->conn);
    return max <= 0 ? 0 : size_t(max);
}

std::optional<deskhub::Fingerprint> QuicEndpoint::PeerFingerprint(QuicConnId conn) const {
    const Impl::Connection* entry = impl_->Lookup(conn);
    if (entry == nullptr) return std::nullopt;
    const uint8_t* der = nullptr;
    size_t len = 0;
    quiche_conn_peer_cert(entry->conn, &der, &len);
    if (der == nullptr || len == 0) return std::nullopt;
    return FingerprintOfCertDer(std::span<const uint8_t>(der, len));
}

bool QuicEndpoint::Established(QuicConnId conn) const {
    const Impl::Connection* entry = impl_->Lookup(conn);
    return entry != nullptr && quiche_conn_is_established(entry->conn);
}

void QuicEndpoint::CloseConnection(QuicConnId conn, uint64_t errorCode, std::string_view reason) {
    Impl::Connection* entry = impl_->Lookup(conn);
    if (entry == nullptr) return;
    quiche_conn_close(entry->conn, true, errorCode,
        reinterpret_cast<const uint8_t*>(reason.data()), reason.size());
    impl_->Flush(*entry);
}

void QuicEndpoint::Close() {
    impl_->Shutdown();
}

bool QuicEndpoint::IsOpen() const {
    return impl_->open_;
}

bool QuicEndpoint::IsServer() const {
    return impl_->server_;
}

bool QuicEndpoint::LastBindAddrInUse() const {
    return impl_->bindAddrInUse_;
}

size_t QuicEndpoint::ConnectionCount() const {
    return impl_->connections_.size();
}

QuicConnId QuicEndpoint::FirstConnection() const {
    if (impl_->connections_.empty()) return 0;
    return impl_->connections_.begin()->first;
}

uint16_t QuicEndpoint::LocalPort() const {
    return impl_->localPort_;
}

std::vector<QuicConnId> QuicEndpoint::Connections() const {
    std::vector<QuicConnId> out;
    out.reserve(impl_->connections_.size());
    for (const auto& [id, entry] : impl_->connections_) out.push_back(id);
    std::sort(out.begin(), out.end());
    return out;
}

}
