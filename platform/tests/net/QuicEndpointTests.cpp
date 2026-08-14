#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/protocol/Wire.h"
#include "deskhubp/net/QuicEndpoint.h"
#include "deskhubp/system/AppDataFile.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/system/HostIdentity.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr uint16_t kTestPort = 47791;
constexpr int kMaxRounds = 400;

struct Peer {
    deskhubp::QuicEndpoint endpoint{};
    deskhubp::QuicConnId conn = 0;
    std::string stream{};
    std::vector<std::vector<uint8_t>> datagrams{};
    std::vector<std::vector<uint8_t>> foreign{};
    bool connected = false;
    bool closed = false;
};

deskhubp::QuicCallbacks HooksFor(Peer& peer) {
    deskhubp::QuicCallbacks hooks;
    hooks.onConnected = [&peer](deskhubp::QuicConnId id, const NetAddr&) {
        peer.conn = id;
        peer.connected = true;
    };
    hooks.onClosed = [&peer](deskhubp::QuicConnId, const NetAddr&) { peer.closed = true; };
    hooks.onStream = [&peer](deskhubp::QuicConnId, uint64_t, std::span<const uint8_t> bytes,
                         bool) {
        peer.stream.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    };
    hooks.onDatagram = [&peer](deskhubp::QuicConnId, std::span<const uint8_t> bytes) {
        peer.datagrams.emplace_back(bytes.begin(), bytes.end());
    };
    hooks.onForeignDatagram = [&peer](const NetAddr&, std::span<const uint8_t> bytes) {
        peer.foreign.emplace_back(bytes.begin(), bytes.end());
    };
    return hooks;
}

void Pump(Peer& a, Peer& b, int rounds) {
    for (int i = 0; i < rounds; ++i) {
        a.endpoint.Poll(NowUs(), 1);
        b.endpoint.Poll(NowUs(), 1);
    }
}

void TestHandshakeStreamAndDatagram() {
    std::printf("[quic] two endpoints shake hands, then carry a stream and a datagram...\n");
    if (!deskhubp::QuicAvailable()) {
        std::printf("[quic] skipped: this build has no QUIC library\n");
        return;
    }

    const std::string savedCert = deskhubp::ReadAppDataFile(deskhubp::kHostCertFileName);
    const std::string savedKey = deskhubp::ReadAppDataFile(deskhubp::kHostKeyFileName);
    deskhubp::ForgetHostIdentity();
    const deskhubp::HostIdentity identity = deskhubp::LoadOrCreateHostIdentity("deskhub-test");
    Check(identity.Valid(), "the host has an identity to present");
    if (!identity.Valid()) return;

    Peer server;
    Peer client;

    deskhubp::QuicSettings serverSettings;
    serverSettings.certPemPath = identity.certPath;
    serverSettings.keyPemPath = identity.keyPath;
    const bool listening =
        server.endpoint.Listen(serverSettings, "127.0.0.1", kTestPort, HooksFor(server));
    Check(listening, "the host binds its own UDP port");
    if (!listening) return;
    Check(server.endpoint.IsServer() && server.endpoint.IsOpen(), "and is open as a server");

    const NetAddr target{0x7F000001u, kTestPort};
    deskhubp::QuicSettings clientSettings;
    const bool dialed =
        client.endpoint.Connect(clientSettings, target, "deskhub-test", HooksFor(client));
    Check(dialed, "the client starts a connection to it");
    if (!dialed) return;

    for (int i = 0; i < kMaxRounds && !(server.connected && client.connected); ++i)
        Pump(client, server, 1);
    Check(client.connected && server.connected, "the TLS handshake completes over real sockets");
    if (!client.connected || !server.connected) return;

    Check(server.endpoint.ConnectionCount() == 1, "the host sees exactly one client");
    Check(client.endpoint.Established(client.conn), "the client agrees it is established");

    const auto fingerprint = client.endpoint.PeerFingerprint(client.conn);
    Check(fingerprint.has_value(), "the client can see the host's certificate");
    Check(fingerprint && *fingerprint == identity.fingerprint,
        "and its fingerprint is the one the host published - this is what TOFU compares");

    const std::string payload = "terminal: echo hello";
    Check(client.endpoint.SendStream(client.conn, deskhubp::kQuicFirstTerminalStream,
              std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(payload.data()),
                  payload.size())),
        "a stream write is accepted");
    for (int i = 0; i < kMaxRounds && server.stream.size() < payload.size(); ++i)
        Pump(client, server, 1);
    Check(server.stream == payload, "and every byte arrives in order on the far side");

    const std::vector<uint8_t> frame(deskhub::kMaxDatagram, 0x5A);
    Check(client.endpoint.MaxDatagramSize(client.conn) >= deskhub::kMaxDatagram,
        "a QUIC datagram is big enough for a video packet");
    Check(client.endpoint.SendDatagram(client.conn, frame), "a datagram write is accepted");
    for (int i = 0; i < kMaxRounds && server.datagrams.empty(); ++i) Pump(client, server, 1);
    Check(server.datagrams.size() == 1 && server.datagrams[0].size() == frame.size(),
        "and it arrives whole on the video path");

    uint8_t beacon[deskhub::kMaxDatagram];
    const size_t beaconSize = deskhub::BuildListSources(beacon);
    Check(client.endpoint.SendRaw(target, std::span<const uint8_t>(beacon, beaconSize)),
        "a plain beacon packet can share the port");
    for (int i = 0; i < kMaxRounds && server.foreign.empty(); ++i) Pump(client, server, 1);
    Check(server.foreign.size() == 1 && server.foreign[0].size() == beaconSize,
        "and reaches the beacon handler instead of the QUIC connection");
    Check(server.stream == payload, "without disturbing the stream");

    client.endpoint.CloseConnection(client.conn, 0, "done");
    Pump(client, server, 20);

    client.endpoint.Close();
    server.endpoint.Close();
    Check(!client.endpoint.IsOpen() && !server.endpoint.IsOpen(), "both endpoints close cleanly");

    if (!savedCert.empty()) deskhubp::WriteAppDataFile(deskhubp::kHostCertFileName, savedCert);
    if (!savedKey.empty()) deskhubp::WriteAppDataFile(deskhubp::kHostKeyFileName, savedKey);
}

void TestUnstartedEndpointIsHarmless() {
    std::printf("[quic] an endpoint that never started refuses everything quietly...\n");
    deskhubp::QuicEndpoint idle;
    Check(!idle.IsOpen() && idle.ConnectionCount() == 0, "it holds no connections");
    Check(idle.FirstConnection() == 0 && idle.Connections().empty(), "and names none");
    const uint8_t byte = 1;
    Check(!idle.SendStream(0, 0, std::span<const uint8_t>(&byte, 1)), "a stream write fails");
    Check(!idle.SendDatagram(0, std::span<const uint8_t>(&byte, 1)), "a datagram write fails");
    Check(!idle.Established(0) && idle.MaxDatagramSize(0) == 0, "nothing is established");
    Check(!idle.PeerFingerprint(0).has_value(), "and there is no peer to fingerprint");
    idle.CloseConnection(0);
    idle.Poll(NowUs(), 1);
    idle.Close();
    Check(true, "polling and closing it does nothing at all");
}

}

void RunQuicEndpointTests() {
    TestHandshakeStreamAndDatagram();
    TestUnstartedEndpointIsHarmless();
}
