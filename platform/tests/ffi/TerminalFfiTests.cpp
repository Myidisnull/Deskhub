#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/protocol/Wire.h"
#include "deskhubp/ffi/TerminalFfi.h"
#include "deskhubp/net/SessionTransport.h"
#include "deskhubp/session/TerminalHost.h"
#include "deskhubp/system/AppDataFile.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/system/HostIdentity.h"
#include "deskhubp/system/PairedDevicesFile.h"
#include "deskhubp/system/Pty.h"
#include "deskhubp/system/TrustStoreFile.h"

#include <atomic>
#include <cstdio>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr uint16_t kFfiTestPort = 47821;

struct FfiHostRig {
    deskhubp::SessionTransport sock{};
    deskhubp::TerminalHost term{};
    std::thread pump{};
    std::atomic<bool> stopFlag{false};

    ~FfiHostRig() {
        Stop();
    }

    bool Start(const deskhubp::HostIdentity& identity, const std::string& passcode) {
        sock.SetRecvTimeout(1);
        deskhubp::QuicSettings settings;
        settings.certPemPath = identity.certPath;
        settings.keyPemPath = identity.keyPath;
        if (!sock.Listen(settings, kFfiTestPort, "127.0.0.1")) return false;

        deskhubp::HostAuthConfig auth;
        auth.identity = identity;
        auth.SetPasscode(deskhubp::LoadOrCreateAuthSalt(), passcode);
        auth.allowNewPairings = true;
        sock.SetHostAuth(std::move(auth), deskhubp::TransportAuthCallbacks{});
        sock.SetOnPeerGone([this](const NetAddr& peer) { term.OnPeerGone(peer); });

        if (!term.Start(sock, std::string(), deskhubp::TerminalHostCallbacks{})) return false;

        pump = std::thread([this] {
            uint8_t buf[deskhub::kMaxRecordSize];
            while (!stopFlag.load(std::memory_order_acquire)) {
                NetAddr from;
                const int n = sock.RecvFrom(buf, sizeof(buf), from);
                if (n <= 0) continue;
                const std::optional<deskhub::CommonHeader> header =
                    deskhub::ParseCommonHeader(std::span<const uint8_t>(buf, size_t(n)));
                if (header && header->chan == deskhub::Chan::Terminal)
                    term.HandleMessage(from, std::span<const uint8_t>(buf, size_t(n)));
            }
        });
        return true;
    }

    void Stop() {
        if (pump.joinable()) {
            stopFlag.store(true, std::memory_order_release);
            pump.join();
        }
        term.Stop();
        sock.Close();
    }
};

bool WaitForMs(const std::function<bool()>& done, int millis) {
    for (int i = 0; i < millis; ++i) {
        if (done()) return true;
        SleepUs(1000);
    }
    return done();
}

bool GridContains(DHTermSession* session, const std::string& needle) {
    DHTermGrid grid{};
    std::vector<DHTermCell> cells(size_t(200) * 100);
    if (!dh_term_grid(session, 0, cells.data(), uint32_t(cells.size()), &grid)) return false;
    std::string text;
    for (uint32_t r = 0; r < grid.rows; ++r) {
        for (uint32_t c = 0; c < grid.cols; ++c) {
            const uint32_t cp = cells[size_t(r) * grid.cols + c].codepoint;
            text += cp >= 0x20 && cp < 0x7F ? char(cp) : ' ';
        }
        text += '\n';
    }
    return text.find(needle) != std::string::npos;
}

}

void RunTerminalFfiTests() {
    std::printf("--- ffi: the terminal surface every mobile client drives ---\n");
    std::printf("[termffi] a shell opens, echoes and resizes through the C ABI...\n");
    if (!deskhubp::QuicAvailable() || deskhubp::DefaultShell().empty()) {
        std::printf("[termffi] skipped: this build has no QUIC library or no shell to host\n");
        return;
    }

    const std::string savedCert = deskhubp::ReadAppDataFile(deskhubp::kHostCertFileName);
    const std::string savedKey = deskhubp::ReadAppDataFile(deskhubp::kHostKeyFileName);
    const std::string savedTrust = deskhubp::ReadAppDataFile(deskhubp::kTrustStoreFileName);
    const std::string savedPaired = deskhubp::ReadAppDataFile(deskhubp::kPairedDevicesFileName);
    deskhubp::ForgetHostIdentity();
    deskhubp::RemoveAppDataFile(deskhubp::kTrustStoreFileName);
    deskhubp::ForgetAllPairedDevices();

    const deskhubp::HostIdentity identity = deskhubp::LoadOrCreateHostIdentity("deskhub-test");
    FfiHostRig rig;
    if (!rig.Start(identity, kTestPasscode)) {
        Check(false, "the host rig starts");
        return;
    }

    std::atomic<int> redraws{0};
    std::atomic<int32_t> lastState{DHTermIdle};
    DHTermCallbacks callbacks{};
    callbacks.onState = [](int32_t state, const char*, void* user) {
        static_cast<std::atomic<int32_t>*>(user)->store(state);
    };
    callbacks.user = &lastState;

    const std::string address = "127.0.0.1:" + std::to_string(kFfiTestPort);
    DHTermSession* session = dh_term_open(address.c_str(), kTestPasscode, 80, 24, &callbacks);
    Check(session != nullptr, "the C ABI opens a shell session");
    if (session == nullptr) {
        rig.Stop();
        return;
    }

    Check(WaitForMs([session] { return dh_term_state(session) == DHTermLive; }, 20000),
        "and it reaches Live with the passcode proved, never sent");
    Check(lastState.load() == DHTermLive, "the state callback saw the same journey");
    Check(rig.term.SessionCount() == 1, "the host lists the shell");

    char fingerprint[64] = {};
    dh_term_fingerprint(session, fingerprint, sizeof(fingerprint));
    Check(std::string(fingerprint).rfind("SHA256:", 0) == 0,
        "the host key is available for a warning dialog to show");

    dh_term_send_text(session, "echo dh-ffi-ok\n");
    Check(WaitForMs([session] { return GridContains(session, "dh-ffi-ok"); }, 30000),
        "typed text runs on the host and the grid comes back through the C ABI");

    DHTermGrid grid{};
    Check(!dh_term_grid(session, 0, nullptr, 0, &grid),
        "asking with no buffer reports the size instead of writing anywhere");
    Check(grid.rows == 24 && grid.cols == 80, "and the size is the one we opened with");

    dh_term_resize(session, 100, 30);
    Check(WaitForMs(
              [&rig] {
                  const std::vector<deskhub::TerminalRecord> open = rig.term.Sessions();
                  return open.size() == 1 && open[0].size == deskhub::TermSize{100, 30};
              },
              10000),
        "a resize through the C ABI reaches the host");

    dh_term_stop(session);
    Check(WaitForMs([&rig] { return rig.term.SessionCount() == 0; }, 10000),
        "closing the session ends the shell on the host");

    rig.Stop();
    if (!savedCert.empty()) deskhubp::WriteAppDataFile(deskhubp::kHostCertFileName, savedCert);
    if (!savedKey.empty()) deskhubp::WriteAppDataFile(deskhubp::kHostKeyFileName, savedKey);
    if (savedTrust.empty())
        deskhubp::RemoveAppDataFile(deskhubp::kTrustStoreFileName);
    else
        deskhubp::WriteAppDataFile(deskhubp::kTrustStoreFileName, savedTrust);
    if (savedPaired.empty())
        deskhubp::RemoveAppDataFile(deskhubp::kPairedDevicesFileName);
    else
        deskhubp::WriteAppDataFile(deskhubp::kPairedDevicesFileName, savedPaired);
}
