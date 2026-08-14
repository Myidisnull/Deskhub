#include <quiche.h>

#ifndef _WIN32
#include <arpa/inet.h>
#endif
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr size_t kMaxDatagram = 1350;
constexpr size_t kConnIdLen = 16;
constexpr uint64_t kTerminalStreamId = 4;

sockaddr_in MakeAddr(uint16_t port) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    return a;
}

quiche_config* MakeConfig(bool server, const std::string& certDir) {
    quiche_config* cfg = quiche_config_new(QUICHE_PROTOCOL_VERSION);
    if (cfg == nullptr) return nullptr;

    if (server) {
        std::string crt = certDir + "/cert.crt";
        std::string key = certDir + "/cert.key";
        if (quiche_config_load_cert_chain_from_pem_file(cfg, crt.c_str()) < 0) return nullptr;
        if (quiche_config_load_priv_key_from_pem_file(cfg, key.c_str()) < 0) return nullptr;
    } else {
        quiche_config_verify_peer(cfg, false);
    }

    static const uint8_t kAlpn[] = { 7, 'd', 'e', 's', 'k', 'h', 'u', 'b' };
    quiche_config_set_application_protos(cfg, kAlpn, sizeof(kAlpn));
    quiche_config_set_max_idle_timeout(cfg, 5000);
    quiche_config_set_max_recv_udp_payload_size(cfg, kMaxDatagram);
    quiche_config_set_max_send_udp_payload_size(cfg, kMaxDatagram);
    quiche_config_set_initial_max_data(cfg, 1 << 20);
    quiche_config_set_initial_max_stream_data_bidi_local(cfg, 1 << 18);
    quiche_config_set_initial_max_stream_data_bidi_remote(cfg, 1 << 18);
    quiche_config_set_initial_max_streams_bidi(cfg, 16);
    quiche_config_set_initial_max_streams_uni(cfg, 16);
    quiche_config_enable_dgram(cfg, true, 128, 128);
    return cfg;
}

size_t Deliver(quiche_conn* from, quiche_conn* to, const sockaddr_in& fromAddr,
    const sockaddr_in& toAddr) {
    uint8_t buf[kMaxDatagram];
    size_t delivered = 0;
    for (;;) {
        quiche_send_info sendInfo{};
        ssize_t written = quiche_conn_send(from, buf, sizeof(buf), &sendInfo);
        if (written == QUICHE_ERR_DONE) break;
        if (written < 0) {
            std::printf("  quiche_conn_send failed: %zd\n", written);
            return delivered;
        }
        quiche_recv_info recvInfo{};
        recvInfo.from = (sockaddr*)&fromAddr;
        recvInfo.from_len = sizeof(fromAddr);
        recvInfo.to = (sockaddr*)&toAddr;
        recvInfo.to_len = sizeof(toAddr);
        ssize_t read = quiche_conn_recv(to, buf, (size_t)written, &recvInfo);
        if (read < 0) {
            std::printf("  quiche_conn_recv failed: %zd\n", read);
            return delivered;
        }
        ++delivered;
    }
    return delivered;
}

bool ReadStream(quiche_conn* conn, uint64_t expectId, std::string& out) {
    quiche_stream_iter* it = quiche_conn_readable(conn);
    uint64_t id = 0;
    bool found = false;
    while (quiche_stream_iter_next(it, &id)) {
        if (id != expectId) continue;
        uint8_t buf[4096];
        bool fin = false;
        uint64_t err = 0;
        ssize_t n = quiche_conn_stream_recv(conn, id, buf, sizeof(buf), &fin, &err);
        if (n > 0) {
            out.assign((const char*)buf, (size_t)n);
            found = true;
        }
    }
    quiche_stream_iter_free(it);
    return found;
}

}

int main(int argc, char** argv) {
    const std::string certDir = argc > 1 ? argv[1] : ".";

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::printf("FAIL: WSAStartup\n");
        return 1;
    }
#endif

    quiche_config* clientCfg = MakeConfig(false, certDir);
    quiche_config* serverCfg = MakeConfig(true, certDir);
    if (clientCfg == nullptr || serverCfg == nullptr) {
        std::printf("FAIL: config setup\n");
        return 1;
    }

    const sockaddr_in clientAddr = MakeAddr(55555);
    const sockaddr_in serverAddr = MakeAddr(47777);

    uint8_t clientScid[kConnIdLen];
    uint8_t serverScid[kConnIdLen];
    for (size_t i = 0; i < kConnIdLen; ++i) {
        clientScid[i] = (uint8_t)(0xA0 + i);
        serverScid[i] = (uint8_t)(0xB0 + i);
    }

    quiche_conn* client = quiche_connect("deskhub-host", clientScid, kConnIdLen,
        (sockaddr*)&clientAddr, sizeof(clientAddr),
        (sockaddr*)&serverAddr, sizeof(serverAddr), clientCfg);
    quiche_conn* server = quiche_accept(serverScid, kConnIdLen, nullptr, 0,
        (sockaddr*)&serverAddr, sizeof(serverAddr),
        (sockaddr*)&clientAddr, sizeof(clientAddr), serverCfg);
    if (client == nullptr || server == nullptr) {
        std::printf("FAIL: connection setup\n");
        return 1;
    }

    int rounds = 0;
    while (rounds < 20 && !(quiche_conn_is_established(client) && quiche_conn_is_established(server))) {
        Deliver(client, server, clientAddr, serverAddr);
        Deliver(server, client, serverAddr, clientAddr);
        ++rounds;
    }

    if (!quiche_conn_is_established(client) || !quiche_conn_is_established(server)) {
        std::printf("FAIL: handshake did not complete in %d rounds\n", rounds);
        return 1;
    }
    std::printf("PASS: TLS handshake completed in %d rounds\n", rounds);

    const uint8_t* alpn = nullptr;
    size_t alpnLen = 0;
    quiche_conn_application_proto(client, &alpn, &alpnLen);
    std::printf("PASS: negotiated ALPN = %.*s\n", (int)alpnLen, (const char*)alpn);

    const std::string streamPayload = "terminal: echo hello";
    uint64_t streamErr = 0;
    ssize_t sent = quiche_conn_stream_send(client, kTerminalStreamId,
        (const uint8_t*)streamPayload.data(), streamPayload.size(), false, &streamErr);
    if (sent < 0) {
        std::printf("FAIL: stream_send returned %zd\n", sent);
        return 1;
    }
    Deliver(client, server, clientAddr, serverAddr);

    std::string received;
    if (!ReadStream(server, kTerminalStreamId, received) || received != streamPayload) {
        std::printf("FAIL: stream payload mismatch (got '%s')\n", received.c_str());
        return 1;
    }
    std::printf("PASS: stream carried %zu bytes intact\n", received.size());

    std::vector<uint8_t> frame(1000, 0x5A);
    ssize_t dsent = quiche_conn_dgram_send(client, frame.data(), frame.size());
    if (dsent < 0) {
        std::printf("FAIL: dgram_send returned %zd\n", dsent);
        return 1;
    }
    Deliver(client, server, clientAddr, serverAddr);

    uint8_t dbuf[kMaxDatagram];
    ssize_t dread = quiche_conn_dgram_recv(server, dbuf, sizeof(dbuf));
    if (dread != (ssize_t)frame.size() || dbuf[0] != 0x5A) {
        std::printf("FAIL: dgram payload mismatch (got %zd bytes)\n", dread);
        return 1;
    }
    std::printf("PASS: datagram carried %zd bytes (video path)\n", dread);

    ssize_t maxDgram = quiche_conn_dgram_max_writable_len(client);
    std::printf("INFO: max writable datagram = %zd bytes\n", maxDgram);

    quiche_conn_free(client);
    quiche_conn_free(server);
    quiche_config_free(clientCfg);
    quiche_config_free(serverCfg);
    std::printf("M0 SMOKE TEST PASSED\n");
    return 0;
}
