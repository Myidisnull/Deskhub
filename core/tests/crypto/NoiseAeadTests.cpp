#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/crypto/Aead.h"
#include "deskhub/crypto/KeyCodec.h"
#include "deskhub/crypto/NoiseXx.h"
#include "deskhub/crypto/TrafficCipher.h"
#include "deskhub/session/ClientSession.h"
#include "deskhub/session/HostSession.h"

#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

using namespace deskhub;

namespace {

void TestAeadRoundTrip() {
    std::printf("[crypto] AEAD seal/open round-trip...\n");
    std::fflush(stdout);
    uint8_t key[crypto::kKeySize];
    Check(TestRandomBytes(key), "key material");
    std::printf("[crypto] key ready\n");
    std::fflush(stdout);
    const uint8_t plain[] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint8_t ad[] = {9, 10};
    uint8_t sealed[sizeof(plain) + crypto::kMacSize];
    Check(crypto::AeadSeal(sealed, key, 7, ad, plain), "seal");
    uint8_t out[sizeof(plain)];
    Check(crypto::AeadOpen(out, key, 7, ad, sealed), "open");
    Check(std::memcmp(out, plain, sizeof(plain)) == 0, "plaintext matches");
    sealed[0] ^= 1;
    Check(!crypto::AeadOpen(out, key, 7, ad, sealed), "tamper fails");
}

void TestNoiseAndEncryptedSession() {
    std::printf("[crypto] Noise_XX + encrypted HostSession/ClientSession...\n");
    struct Wire {
        std::deque<Datagram> toHost, toClient;
    } w;
    uint64_t now = 20'000'000;
    crypto::TrafficCipher hostTraffic;
    crypto::KeyPair hostStatic{};
    Check(crypto::GenerateKeyPair(hostStatic, TestRandomBytes), "host static");

    HostCallbacks hcb;
    hcb.send = [&](std::span<const uint8_t> d) { w.toClient.emplace_back(d.begin(), d.end()); };
    hcb.randomBytes = TestRandomBytes;
    auto host = std::make_unique<HostSession>(hcb, StreamParams{1280, 720, 60, 8'000'000});
    host->SetPasscode(kTestPasscode);
    host->SetEncryptRequired(true);
    host->SetEscrowKey(true);
    host->SetHostStaticKey(hostStatic);
    host->SetTrafficCipher(&hostTraffic);

    bool ready = false;
    ClientCallbacks ccb;
    ccb.randomBytes = TestRandomBytes;
    ccb.send = [&](std::span<const uint8_t> d) { w.toHost.emplace_back(d.begin(), d.end()); };
    ccb.onReady = [&](const NegotiatedParams&) { ready = true; };
    auto cli = std::make_unique<ClientSession>(ccb);

    auto pump = [&] {
        for (int guard = 0; guard < 16; ++guard) {
            if (w.toHost.empty() && w.toClient.empty()) break;
            while (!w.toHost.empty()) {
                auto d = std::move(w.toHost.front());
                w.toHost.pop_front();
                host->HandlePacket(d, now, kTestViewer);
            }
            while (!w.toClient.empty()) {
                auto d = std::move(w.toClient.front());
                w.toClient.pop_front();
                cli->HandlePacket(d, now);
            }
        }
    };

    cli->Start(Hello{0xA1B2C3D4, kCodecMaskH264, 1920, 1080, 60, 0, 0, kTestPasscode}, now);
    pump();
    Check(ready, "encrypted handshake reaches HELLO_ACK");
    Check(hostTraffic.hasKey() && cli->encrypted(), "both sides hold the traffic key");
    Check(host->sessionId() == cli->sessionId() && host->sessionId() != 0, "session id agreed");

    w.toHost.clear();
    w.toClient.clear();
    crypto::TrafficCipher rejectTraffic;
    HostCallbacks plainCb;
    plainCb.send = [&](std::span<const uint8_t> d) { w.toClient.emplace_back(d.begin(), d.end()); };
    plainCb.randomBytes = TestRandomBytes;
    auto plainHost = std::make_unique<HostSession>(plainCb, StreamParams{640, 480, 30, 2'000'000});
    plainHost->SetPasscode(kTestPasscode);
    plainHost->SetEncryptRequired(true);
    plainHost->SetEscrowKey(true);
    plainHost->SetHostStaticKey(hostStatic);
    plainHost->SetTrafficCipher(&rejectTraffic);
    uint8_t helloBuf[kMaxDatagram];
    Hello h{0x1111, kCodecMaskH264, 800, 600, 30, 0, 0, kTestPasscode};
    const size_t hn = BuildHello(helloBuf, h);
    plainHost->HandlePacket(std::span<const uint8_t>(helloBuf, hn), now, kTestViewer + 1);
    Check(!w.toClient.empty(), "plaintext HELLO is answered");
    const auto ack = ParseHelloAck(PayloadOf(w.toClient.back()));
    Check(ack && ack->codec == Codec::Rejected && ack->reason == RejectReason::EncryptionRequired,
        "plaintext HELLO rejected with EncryptionRequired");
}

void TestEncryptedVideoFlagsSurvive() {
    std::printf("[crypto] sealed video packets keep IDR/frame-end flags...\n");
    uint8_t key[crypto::kKeySize];
    Check(TestRandomBytes(key), "key material");
    crypto::TrafficCipher c;
    c.SetKey(key);

    uint8_t clear[256];
    const uint8_t payload[] = {0x00, 0x00, 0x00, 0x01, 0x67};
    VideoHeader vh{};
    vh.frameId = 7;
    vh.pktIndex = 0;
    vh.pktCount = 1;
    vh.timestampUs = 0;
    const size_t clearN =
        BuildVideoPacket(clear, 0xABCDu, vh, true, true, std::span<const uint8_t>(payload));
    Check(clearN > 0, "clear video packet");
    const auto clearH = ParseCommonHeader(std::span<const uint8_t>(clear, clearN));
    Check(clearH && (clearH->flags & kVideoFlagIdr) != 0, "clear IDR bit set");
    Check((clearH->flags & crypto::kHdrFlagEncrypted) == 0, "clear is not marked encrypted");

    uint8_t sealed[512];
    const size_t sealedN = c.SealDatagram(sealed, std::span<const uint8_t>(clear, clearN));
    Check(sealedN > clearN, "seal grows the datagram");
    const auto sealedH = ParseCommonHeader(std::span<const uint8_t>(sealed, sealedN));
    Check(sealedH && (sealedH->flags & crypto::kHdrFlagEncrypted) != 0, "wire marks encrypted");
    Check((sealedH->flags & kVideoFlagIdr) != 0, "wire still carries IDR");

    auto opened = c.OpenDatagram(std::span<const uint8_t>(sealed, sealedN));
    Check(opened.has_value(), "open succeeds");
    const auto openH = ParseCommonHeader(*opened);
    Check(openH && (openH->flags & crypto::kHdrFlagEncrypted) == 0, "opened clears encrypt bit");
    Check((openH->flags & kVideoFlagIdr) != 0 && (openH->flags & kVideoFlagFrameEnd) != 0,
        "opened keeps video flags");
    const auto v = ParseVideoPacket(*openH, PayloadOf(*opened));
    Check(v && v->idr && v->frameEnd, "parsed video still IDR + frame end");
}

void TestKeyHex() {
    std::printf("[crypto] host static key hex codec...\n");
    crypto::KeyPair kp{};
    Check(crypto::GenerateKeyPair(kp, TestRandomBytes), "keypair");
    std::string hex = crypto::KeyToHex(std::span<const uint8_t>(kp.sk, crypto::kKeySize));
    Check(hex.size() == 64, "hex width");
    uint8_t sk2[crypto::kKeySize];
    Check(crypto::KeyFromHex(hex, sk2), "parse");
    Check(std::memcmp(sk2, kp.sk, crypto::kKeySize) == 0, "round-trip");
}

void TestTwoViewersShareTrafficKey() {
    std::printf("[crypto] two encrypted viewers share one traffic key without AEAD clash...\n");
    uint64_t now = 30'000'000;
    crypto::TrafficCipher hostTraffic;
    crypto::KeyPair hostStatic{};
    Check(crypto::GenerateKeyPair(hostStatic, TestRandomBytes), "host static");

    std::deque<Datagram> toA, toB, fromA, fromB;
    uint64_t replyAddr = 0;
    HostCallbacks hcb;
    hcb.send = [&](std::span<const uint8_t> d) {
        auto& q = (replyAddr == kTestViewer + 2) ? toB : toA;
        q.emplace_back(d.begin(), d.end());
    };
    hcb.sendTo = [&](uint64_t addr, std::span<const uint8_t> d) {
        auto& q = (addr == kTestViewer) ? toA : toB;
        q.emplace_back(d.begin(), d.end());
    };
    hcb.randomBytes = TestRandomBytes;
    auto host = std::make_unique<HostSession>(hcb, StreamParams{1280, 720, 60, 4'000'000});
    host->SetPasscode(kTestPasscode);
    host->SetEncryptRequired(true);
    host->SetEscrowKey(true);
    host->SetHostStaticKey(hostStatic);
    host->SetTrafficCipher(&hostTraffic);

    bool readyA = false, readyB = false;
    ClientCallbacks ca, cb;
    ca.randomBytes = TestRandomBytes;
    cb.randomBytes = TestRandomBytes;
    ca.send = [&](std::span<const uint8_t> d) { fromA.emplace_back(d.begin(), d.end()); };
    cb.send = [&](std::span<const uint8_t> d) { fromB.emplace_back(d.begin(), d.end()); };
    ca.onReady = [&](const NegotiatedParams&) { readyA = true; };
    cb.onReady = [&](const NegotiatedParams&) { readyB = true; };
    auto cliA = std::make_unique<ClientSession>(ca);
    auto cliB = std::make_unique<ClientSession>(cb);

    auto pumpOne = [&](ClientSession& cli, std::deque<Datagram>& from, std::deque<Datagram>& to,
                       uint64_t addr) {
        replyAddr = addr;
        for (int guard = 0; guard < 12; ++guard) {
            if (from.empty() && to.empty()) break;
            while (!from.empty()) {
                auto d = std::move(from.front());
                from.pop_front();
                host->HandlePacket(d, now, addr);
            }
            while (!to.empty()) {
                auto d = std::move(to.front());
                to.pop_front();
                cli.HandlePacket(d, now);
            }
        }
    };

    cliA->Start(Hello{0xA, kCodecMaskH264, 1280, 720, 60, 0, 0, kTestPasscode}, now);
    pumpOne(*cliA, fromA, toA, kTestViewer);
    Check(readyA && cliA->encrypted(), "viewer A ready with traffic key");
    cliA->Tick(now);
    pumpOne(*cliA, fromA, toA, kTestViewer);
    Check(host->viewerCount() == 1, "viewer A admitted after Start");

    cliB->Start(Hello{0xB, kCodecMaskH264, 1280, 720, 60, 0, 0, kTestPasscode}, now);
    pumpOne(*cliB, fromB, toB, kTestViewer + 2);
    Check(readyB && cliB->encrypted(), "viewer B ready with traffic key");
    cliB->Tick(now);
    pumpOne(*cliB, fromB, toB, kTestViewer + 2);
    Check(host->viewerCount() == 2 && hostTraffic.hasKey(), "both viewers share host traffic key");

    now += 1'100'000;
    cliA->Tick(now);
    cliB->Tick(now);
    pumpOne(*cliA, fromA, toA, kTestViewer);
    pumpOne(*cliB, fromB, toB, kTestViewer + 2);
    Check(cliA->state() != ClientSession::State::Dead && cliB->state() != ClientSession::State::Dead,
        "both encrypted viewers stay alive after concurrent control traffic");
}

void TestPskEncryptedSession() {
    std::printf("[crypto] escrow-off PSK HostSession/ClientSession...\n");
    struct Wire {
        std::deque<Datagram> toHost, toClient;
    } w;
    uint64_t now = 40'000'000;
    crypto::TrafficCipher hostTraffic;
    crypto::KeyPair hostStatic{};
    Check(crypto::GenerateKeyPair(hostStatic, TestRandomBytes), "host static");
    uint8_t session[crypto::kKeySize];
    Check(TestRandomBytes(session), "session key");

    HostCallbacks hcb;
    hcb.send = [&](std::span<const uint8_t> d) { w.toClient.emplace_back(d.begin(), d.end()); };
    hcb.randomBytes = TestRandomBytes;
    auto host = std::make_unique<HostSession>(hcb, StreamParams{1280, 720, 60, 8'000'000});
    host->SetPasscode(kTestPasscode);
    host->SetEncryptRequired(true);
    host->SetEscrowKey(false);
    host->SetHostStaticKey(hostStatic);
    host->SetSessionKey(session);
    host->SetTrafficCipher(&hostTraffic);

    bool ready = false;
    ClientCallbacks ccb;
    ccb.randomBytes = TestRandomBytes;
    ccb.send = [&](std::span<const uint8_t> d) { w.toHost.emplace_back(d.begin(), d.end()); };
    ccb.onReady = [&](const NegotiatedParams&) { ready = true; };
    auto cli = std::make_unique<ClientSession>(ccb);
    cli->SetSessionKey(session);

    auto pump = [&] {
        for (int guard = 0; guard < 16; ++guard) {
            if (w.toHost.empty() && w.toClient.empty()) break;
            while (!w.toHost.empty()) {
                auto d = std::move(w.toHost.front());
                w.toHost.pop_front();
                host->HandlePacket(d, now, kTestViewer);
            }
            while (!w.toClient.empty()) {
                auto d = std::move(w.toClient.front());
                w.toClient.pop_front();
                cli->HandlePacket(d, now);
            }
        }
    };

    cli->Start(Hello{0xC0FFEE, kCodecMaskH264, 1920, 1080, 60, 0, 0, kTestPasscode}, now);
    pump();
    Check(ready, "PSK handshake reaches HELLO_ACK");
    Check(hostTraffic.hasKey() && cli->encrypted(), "both sides hold the shared session key");
    cli->Tick(now);
    pump();
    Check(host->viewerCount() == 1, "encrypted Start proves the key and admits the viewer");
}

void TestEncryptedSessionRejectsCleartextInput() {
    std::printf("[crypto] encrypted session rejects cleartext input...\n");
    struct Wire {
        std::deque<Datagram> toHost, toClient;
    } w;
    uint64_t now = 50'000'000;
    crypto::TrafficCipher hostTraffic;
    crypto::KeyPair hostStatic{};
    Check(crypto::GenerateKeyPair(hostStatic, TestRandomBytes), "host static");

    int inputs = 0;
    HostCallbacks hcb;
    hcb.send = [&](std::span<const uint8_t> d) { w.toClient.emplace_back(d.begin(), d.end()); };
    hcb.randomBytes = TestRandomBytes;
    hcb.onInput = [&](const InputEvent&) { ++inputs; };
    hcb.onStart = [&] {};
    auto host = std::make_unique<HostSession>(hcb, StreamParams{1280, 720, 60, 4'000'000});
    host->SetPasscode(kTestPasscode);
    host->SetEncryptRequired(true);
    host->SetEscrowKey(true);
    host->SetHostStaticKey(hostStatic);
    host->SetTrafficCipher(&hostTraffic);

    bool ready = false;
    ClientCallbacks ccb;
    ccb.randomBytes = TestRandomBytes;
    ccb.send = [&](std::span<const uint8_t> d) { w.toHost.emplace_back(d.begin(), d.end()); };
    ccb.onReady = [&](const NegotiatedParams&) { ready = true; };
    auto cli = std::make_unique<ClientSession>(ccb);

    auto pump = [&] {
        for (int guard = 0; guard < 16; ++guard) {
            if (w.toHost.empty() && w.toClient.empty()) break;
            while (!w.toHost.empty()) {
                auto d = std::move(w.toHost.front());
                w.toHost.pop_front();
                host->HandlePacket(d, now, kTestViewer);
            }
            while (!w.toClient.empty()) {
                auto d = std::move(w.toClient.front());
                w.toClient.pop_front();
                cli->HandlePacket(d, now);
            }
        }
    };

    cli->Start(Hello{0xD00D, kCodecMaskH264, 1280, 720, 60, 0, 0, kTestPasscode}, now);
    pump();
    Check(ready, "handshake ready");
    cli->Tick(now);
    pump();
    Check(host->viewerCount() == 1 && host->state() == HostSession::State::Streaming, "streaming");

    InputEvent ev{InputType::Key, 0, 0x41, 0, 1, 0};
    uint8_t clearIn[kMaxDatagram];
    const size_t n = BuildInputEvents(clearIn, host->sessionId(), 1, std::span<const InputEvent>(&ev, 1));
    Check(n > 0, "clear input packet");
    Check(!host->HandlePacket(std::span<const uint8_t>(clearIn, n), now, kTestViewer),
        "cleartext input is refused");
    Check(inputs == 0, "no input applied from cleartext");
}

void TestWrongPskDoesNotOccupyViewerSlot() {
    std::printf("[crypto] wrong PSK does not occupy a viewer seat...\n");
    struct Wire {
        std::deque<Datagram> toHost, toClient;
    } w;
    uint64_t now = 60'000'000;
    crypto::TrafficCipher hostTraffic;
    crypto::KeyPair hostStatic{};
    Check(crypto::GenerateKeyPair(hostStatic, TestRandomBytes), "host static");
    uint8_t good[crypto::kKeySize];
    uint8_t bad[crypto::kKeySize];
    Check(TestRandomBytes(good) && TestRandomBytes(bad), "keys");
    bad[0] ^= 0x5A;

    HostCallbacks hcb;
    hcb.send = [&](std::span<const uint8_t> d) { w.toClient.emplace_back(d.begin(), d.end()); };
    hcb.randomBytes = TestRandomBytes;
    auto host = std::make_unique<HostSession>(hcb, StreamParams{800, 600, 30, 2'000'000});
    host->SetPasscode(kTestPasscode);
    host->SetEncryptRequired(true);
    host->SetEscrowKey(false);
    host->SetHostStaticKey(hostStatic);
    host->SetSessionKey(good);
    host->SetTrafficCipher(&hostTraffic);

    const char* ended = nullptr;
    ClientCallbacks ccb;
    ccb.randomBytes = TestRandomBytes;
    ccb.send = [&](std::span<const uint8_t> d) { w.toHost.emplace_back(d.begin(), d.end()); };
    ccb.onDisconnect = [&](const char* reason) { ended = reason; };
    auto cli = std::make_unique<ClientSession>(ccb);
    cli->SetSessionKey(bad);

    auto pump = [&] {
        for (int guard = 0; guard < 16; ++guard) {
            if (w.toHost.empty() && w.toClient.empty()) break;
            while (!w.toHost.empty()) {
                auto d = std::move(w.toHost.front());
                w.toHost.pop_front();
                host->HandlePacket(d, now, kTestViewer);
            }
            while (!w.toClient.empty()) {
                auto d = std::move(w.toClient.front());
                w.toClient.pop_front();
                cli->HandlePacket(d, now);
            }
        }
    };

    cli->Start(Hello{0xBAD, kCodecMaskH264, 800, 600, 30, 0, 0, kTestPasscode}, now);
    pump();
    Check(cli->state() == ClientSession::State::Dead, "wrong key kills the client");
    Check(cli->rejectReason() == RejectReason::WrongSessionKey, "names WrongSessionKey");
    Check(ended && std::strstr(ended, "session key"), "die reason mentions session key");
    Check(host->viewerCount() == 0, "host seat stays free without key proof");
}

void TestConcurrentNoiseHandshakes() {
    std::printf("[crypto] concurrent Noise handshakes keep separate pending slots...\n");
    uint64_t now = 70'000'000;
    crypto::TrafficCipher hostTraffic;
    crypto::KeyPair hostStatic{};
    Check(crypto::GenerateKeyPair(hostStatic, TestRandomBytes), "host static");

    std::deque<Datagram> toA, toB, fromA, fromB;
    uint64_t replyAddr = 0;
    HostCallbacks hcb;
    hcb.send = [&](std::span<const uint8_t> d) {
        auto& q = (replyAddr == kTestViewer + 2) ? toB : toA;
        q.emplace_back(d.begin(), d.end());
    };
    hcb.randomBytes = TestRandomBytes;
    auto host = std::make_unique<HostSession>(hcb, StreamParams{1280, 720, 60, 4'000'000});
    host->SetPasscode(kTestPasscode);
    host->SetEncryptRequired(true);
    host->SetEscrowKey(true);
    host->SetHostStaticKey(hostStatic);
    host->SetTrafficCipher(&hostTraffic);

    bool readyA = false, readyB = false;
    ClientCallbacks ca, cb;
    ca.randomBytes = TestRandomBytes;
    cb.randomBytes = TestRandomBytes;
    ca.send = [&](std::span<const uint8_t> d) { fromA.emplace_back(d.begin(), d.end()); };
    cb.send = [&](std::span<const uint8_t> d) { fromB.emplace_back(d.begin(), d.end()); };
    ca.onReady = [&](const NegotiatedParams&) { readyA = true; };
    cb.onReady = [&](const NegotiatedParams&) { readyB = true; };
    auto cliA = std::make_unique<ClientSession>(ca);
    auto cliB = std::make_unique<ClientSession>(cb);

    cliA->Start(Hello{0xA, kCodecMaskH264, 1280, 720, 60, 0, 0, kTestPasscode}, now);
    cliB->Start(Hello{0xB, kCodecMaskH264, 1280, 720, 60, 0, 0, kTestPasscode}, now);

    for (int guard = 0; guard < 24; ++guard) {
        replyAddr = kTestViewer;
        while (!fromA.empty()) {
            auto d = std::move(fromA.front());
            fromA.pop_front();
            host->HandlePacket(d, now, kTestViewer);
        }
        while (!toA.empty()) {
            auto d = std::move(toA.front());
            toA.pop_front();
            cliA->HandlePacket(d, now);
        }
        replyAddr = kTestViewer + 2;
        while (!fromB.empty()) {
            auto d = std::move(fromB.front());
            fromB.pop_front();
            host->HandlePacket(d, now, kTestViewer + 2);
        }
        while (!toB.empty()) {
            auto d = std::move(toB.front());
            toB.pop_front();
            cliB->HandlePacket(d, now);
        }
        if (readyA && readyB) break;
    }
    Check(readyA && readyB, "both overlapping handshakes complete");
    cliA->Tick(now);
    cliB->Tick(now);
    replyAddr = kTestViewer;
    while (!fromA.empty()) {
        auto d = std::move(fromA.front());
        fromA.pop_front();
        host->HandlePacket(d, now, kTestViewer);
    }
    replyAddr = kTestViewer + 2;
    while (!fromB.empty()) {
        auto d = std::move(fromB.front());
        fromB.pop_front();
        host->HandlePacket(d, now, kTestViewer + 2);
    }
    Check(host->viewerCount() == 2, "both viewers admitted after key proof");
}

}

void RunNoiseAeadTests() {
    TestAeadRoundTrip();
    TestKeyHex();
    TestEncryptedVideoFlagsSurvive();
    TestNoiseAndEncryptedSession();
    TestTwoViewersShareTrafficKey();
    TestPskEncryptedSession();
    TestEncryptedSessionRejectsCleartextInput();
    TestWrongPskDoesNotOccupyViewerSlot();
    TestConcurrentNoiseHandshakes();
}
