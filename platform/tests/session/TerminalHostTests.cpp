#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/protocol/RecordStream.h"
#include "deskhub/session/TerminalClient.h"
#include "deskhub/terminal/KeyEncoder.h"
#include "deskhub/terminal/Screen.h"
#include "deskhubp/net/QuicEndpoint.h"
#include "deskhubp/session/TerminalHost.h"
#include "deskhubp/session/TerminalViewer.h"
#include "deskhubp/system/TrustStoreFile.h"
#include "deskhubp/system/AppDataFile.h"
#include "deskhubp/system/Clock.h"

#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr uint16_t kTestPort = 47793;
constexpr int kMaxRounds = 2000;
constexpr uint32_t kPollWaitMs = 2;

// One terminal client speaking the real protocol over a real QUIC connection:
// records on a stream, a TerminalClient driving them, and a Screen showing what
// the shell drew. This is the whole M7 path with nothing stubbed out.
struct Viewer {
    deskhubp::QuicEndpoint endpoint{};
    deskhub::RecordStream framer{};
    deskhub::term::Screen screen{deskhub::TermSize{80, 24}};
    deskhubp::QuicConnId conn = 0;
    bool connected = false;
    std::vector<uint8_t> pending{};
    std::vector<deskhub::TermReason> refusals{};
    std::vector<int32_t> exits{};
    size_t opens = 0;
    bool resumed = false;

    std::unique_ptr<deskhub::TerminalClient> client{};

    void Start() {
        deskhub::TerminalClientCallbacks cb;
        cb.send = [this](std::span<const uint8_t> message) {
            std::vector<uint8_t> record(deskhub::kRecordPrefixSize + message.size());
            record.resize(deskhub::BuildRecord(record, message));
            if (connected)
                endpoint.SendStream(conn, deskhubp::kQuicFirstTerminalStream, record);
            else
                pending.insert(pending.end(), record.begin(), record.end());
        };
        cb.onOutput = [this](std::span<const uint8_t> bytes) { screen.Write(bytes); };
        cb.onOpened = [this](const deskhub::TermOpenAck& ack) {
            ++opens;
            resumed = ack.resumed;
        };
        cb.onRefused = [this](deskhub::TermReason reason) { refusals.push_back(reason); };
        cb.onExit = [this](int32_t code) { exits.push_back(code); };
        client = std::make_unique<deskhub::TerminalClient>(std::move(cb));
    }

    deskhubp::QuicCallbacks Hooks() {
        deskhubp::QuicCallbacks hooks;
        hooks.onConnected = [this](deskhubp::QuicConnId id, const NetAddr&) {
            conn = id;
            connected = true;
            if (!pending.empty()) {
                endpoint.SendStream(id, deskhubp::kQuicFirstTerminalStream, pending);
                pending.clear();
            }
        };
        hooks.onStream = [this](deskhubp::QuicConnId, uint64_t, std::span<const uint8_t> bytes,
                             bool) {
            framer.Append(bytes);
            std::vector<uint8_t> message;
            while (framer.Next(message)) client->HandleMessage(message);
        };
        return hooks;
    }

    void Pump(int rounds) {
        for (int i = 0; i < rounds; ++i) endpoint.Poll(NowUs(), kPollWaitMs);
    }

    bool PumpUntil(const std::function<bool()>& done, int rounds) {
        for (int i = 0; i < rounds; ++i) {
            if (done()) return true;
            endpoint.Poll(NowUs(), kPollWaitMs);
        }
        return done();
    }

    void Type(std::string_view text) {
        const std::string bytes = deskhub::term::EncodeText(text, screen.Modes());
        client->SendInput(std::span<const uint8_t>(
            reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size()));
    }
};

void TestHostSharesAShell() {
    std::printf("[termhost] a client opens a real shell over QUIC and gets its output back...\n");
    if (!deskhubp::QuicAvailable() || deskhubp::DefaultShell().empty()) {
        std::printf("[termhost] skipped: this build has no QUIC library or no shell to host\n");
        return;
    }

    const std::string savedCert = deskhubp::ReadAppDataFile(deskhubp::kHostCertFileName);
    const std::string savedKey = deskhubp::ReadAppDataFile(deskhubp::kHostKeyFileName);
    deskhubp::ForgetHostIdentity();
    const deskhubp::HostIdentity identity = deskhubp::LoadOrCreateHostIdentity("deskhub-test");
    Check(identity.Valid(), "the host has an identity to present");
    if (!identity.Valid()) return;

    std::vector<std::string> audit;
    deskhubp::TerminalHostConfig config;
    config.bindIp = "127.0.0.1";
    config.port = kTestPort;
    config.passcode = kTestPasscode;

    deskhubp::TerminalHostCallbacks hostHooks;
    hostHooks.onAudit = [&audit](std::string_view line) { audit.emplace_back(line); };

    deskhubp::TerminalHost host;
    const bool started = host.Start(config, identity, std::move(hostHooks));
    Check(started, "the terminal host binds its own listener");
    if (!started) return;
    Check(host.Running() && host.Port() == kTestPort, "and reports itself as sharing");

    Viewer viewer;
    viewer.Start();
    Check(viewer.endpoint.Connect(deskhubp::QuicSettings{}, NetAddr{0x7F000001u, kTestPort},
              "deskhub-test", viewer.Hooks()),
        "a client can dial it");
    Check(viewer.PumpUntil([&viewer] { return viewer.connected; }, kMaxRounds),
        "and the handshake completes");
    if (!viewer.connected) {
        host.Stop();
        return;
    }

    viewer.client->Open("9999", deskhub::TermSize{80, 24}, "test-client");
    Check(viewer.PumpUntil([&viewer] { return !viewer.refusals.empty(); }, kMaxRounds),
        "a wrong passcode is refused");
    Check(!viewer.refusals.empty() && viewer.refusals[0] == deskhub::TermReason::WrongPasscode,
        "and the client is told why");
    Check(host.SessionCount() == 0, "no shell was started for it");

    viewer.client->Open(kTestPasscode, deskhub::TermSize{80, 24}, "test-client");
    Check(viewer.PumpUntil([&viewer] { return viewer.opens == 1; }, kMaxRounds),
        "the right passcode opens a shell");
    if (viewer.opens != 1) {
        host.Stop();
        return;
    }
    Check(!viewer.resumed, "which is a fresh one, not a resumed session");
    Check(host.SessionCount() == 1, "and the host lists it");

    const std::vector<deskhub::TerminalRecord> sessions = host.Sessions();
    Check(sessions.size() == 1 && sessions[0].clientName == "test-client",
        "the host knows who opened it");
    Check(!sessions.empty() && !sessions[0].clientEndpoint.empty(),
        "and where it came from");
    Check(!audit.empty() && audit[0].find("terminal opened") == 0,
        "the session is written to the audit log");
    Check(!audit.empty() && audit[0].find("key=none") != std::string::npos,
        "which says plainly that this client offered no certificate of its own");

    viewer.PumpUntil([] { return false; }, 400);
    viewer.Type("echo deskhub-remote-ok\n");
    const bool sawIt = viewer.PumpUntil(
        [&viewer] {
            return viewer.screen.Text().find("deskhub-remote-ok") != std::string::npos;
        },
        kMaxRounds * 3);
    Check(sawIt, "a command typed on the client runs on the host and comes back to the grid");

    viewer.client->Resize(deskhub::TermSize{120, 40});
    viewer.Pump(100);
    Check(host.Sessions().size() == 1 && host.Sessions()[0].size == deskhub::TermSize{120, 40},
        "a window resize reaches the host");

    viewer.client->Close();
    Check(viewer.PumpUntil([&host] { return host.SessionCount() == 0; }, kMaxRounds),
        "closing the shell from the client ends the session on the host");

    viewer.client->Open(kTestPasscode, deskhub::TermSize{80, 24}, "test-client");
    if (viewer.PumpUntil([&viewer] { return viewer.opens == 2; }, kMaxRounds)) {
        const std::vector<deskhub::TerminalRecord> open = host.Sessions();
        Check(open.size() == 1, "a second shell opens after the first one ended");
        host.KickSession(open[0].termId);
        Check(viewer.PumpUntil([&host] { return host.SessionCount() == 0; }, kMaxRounds),
            "and the host can end a shell from its own table");
        Check(viewer.PumpUntil([&viewer] { return !viewer.exits.empty(); }, kMaxRounds),
            "telling the client the shell is gone");
    }

    host.Stop();
    Check(!host.Running(), "and the host stops sharing cleanly");
    viewer.endpoint.Close();

    if (!savedCert.empty()) deskhubp::WriteAppDataFile(deskhubp::kHostCertFileName, savedCert);
    if (!savedKey.empty()) deskhubp::WriteAppDataFile(deskhubp::kHostKeyFileName, savedKey);
}

bool WaitFor(const std::function<bool()>& done, int millis) {
    for (int i = 0; i < millis; ++i) {
        if (done()) return true;
        SleepUs(1000);
    }
    return done();
}

void TestViewerTrustsThenRunsAShell() {
    std::printf("[termhost] the shared viewer asks about a new key, then runs a shell...\n");
    if (!deskhubp::QuicAvailable() || deskhubp::DefaultShell().empty()) {
        std::printf("[termhost] skipped: this build has no QUIC library or no shell to host\n");
        return;
    }

    const std::string savedCert = deskhubp::ReadAppDataFile(deskhubp::kHostCertFileName);
    const std::string savedKey = deskhubp::ReadAppDataFile(deskhubp::kHostKeyFileName);
    const std::string savedTrust = deskhubp::ReadAppDataFile(deskhubp::kTrustStoreFileName);
    deskhubp::ForgetHostIdentity();
    deskhubp::RemoveAppDataFile(deskhubp::kTrustStoreFileName);

    const deskhubp::HostIdentity identity = deskhubp::LoadOrCreateHostIdentity("deskhub-test");
    deskhubp::TerminalHostConfig config;
    config.bindIp = "127.0.0.1";
    config.port = uint16_t(kTestPort + 1);
    config.passcode = kTestPasscode;

    deskhubp::TerminalHost host;
    if (!host.Start(config, identity, deskhubp::TerminalHostCallbacks{})) {
        Check(false, "the terminal host starts");
        return;
    }

    deskhubp::TerminalViewerConfig viewerConfig;
    viewerConfig.host = NetAddr{0x7F000001u, config.port};
    viewerConfig.hostLabel = "deskhub-test";
    viewerConfig.passcode = kTestPasscode;
    viewerConfig.clientName = "shared-viewer";
    viewerConfig.size = deskhub::TermSize{80, 24};

    std::atomic<int> trustAsks{0};
    std::atomic<int> redraws{0};
    deskhubp::TerminalViewerCallbacks hooks;
    hooks.onTrustAsked = [&trustAsks](deskhub::TrustVerdict, std::string_view) { ++trustAsks; };
    hooks.onRedraw = [&redraws] { ++redraws; };

    deskhubp::TerminalViewer viewer;
    Check(viewer.Start(viewerConfig, std::move(hooks)), "the viewer starts");
    Check(WaitFor([&viewer] { return viewer.State() == deskhubp::TerminalViewerState::Deciding; },
              15000),
        "a machine we have never met stops to ask about its key");
    Check(trustAsks == 1, "and asks exactly once");
    Check(viewer.Verdict() == deskhub::TrustVerdict::Unknown, "reporting it as a stranger");
    Check(viewer.Fingerprint() == deskhub::FormatFingerprint(identity.fingerprint),
        "and showing the fingerprint the host actually holds");

    viewer.AcceptFingerprint();
    Check(WaitFor([&viewer] { return viewer.State() == deskhubp::TerminalViewerState::Live; },
              20000),
        "trusting it opens the shell");

    viewer.SendText("echo deskhub-viewer-ok\n");
    Check(WaitFor(
              [&viewer] {
                  const deskhubp::TerminalSnapshot shot = viewer.Snapshot();
                  for (uint16_t r = 0; r < shot.size.rows; ++r) {
                      std::string line;
                      for (uint16_t c = 0; c < shot.size.cols; ++c)
                          line += deskhub::term::EncodeUtf8(shot.At(r, c).ch);
                      if (line.find("deskhub-viewer-ok") != std::string::npos) return true;
                  }
                  return false;
              },
              30000),
        "and what the shell prints reaches the grid the client draws");
    Check(redraws > 0, "the client was told to repaint");

    const deskhubp::TerminalSnapshot shot = viewer.Snapshot();
    Check(shot.size == deskhub::TermSize{80, 24} &&
              shot.cells.size() == size_t(shot.size.rows) * shot.size.cols,
        "the snapshot holds one cell per position");
    Check(shot.At(999, 999) == deskhub::term::Cell{}, "and reading outside it is blank, not a crash");
    Check(shot.scrollOffset == 0 && shot.cursor.visible,
        "the live view is at the bottom, with the cursor on it");

    const std::string marker = "deskhub-scrollback-marker";
    viewer.SendText("echo " + marker + "\n");
    for (int i = 0; i < 60; ++i) viewer.SendText("echo filler-" + std::to_string(i) + "\n");
    Check(WaitFor([&viewer] { return viewer.Snapshot().scrollbackRows > 24; }, 30000),
        "enough output scrolls the first lines off the top and into the scrollback");

    const deskhubp::TerminalSnapshot back = viewer.Snapshot(viewer.Snapshot().scrollbackRows);
    Check(back.scrollOffset > 0, "the view can be walked back into it");
    Check(back.size == shot.size && back.cells.size() == shot.cells.size(),
        "a scrolled view is the same shape as the live one");
    Check(!back.cursor.visible, "with no cursor drawn, because it is not on this screen");
    Check(viewer.Snapshot(999999).scrollOffset == back.scrollOffset,
        "scrolling past the oldest line stops there instead of running off the end");

    viewer.Stop();
    host.Stop();

    const deskhub::TrustStore trusted = deskhubp::LoadTrustStore();
    Check(trusted.Size() == 1, "the machine we trusted was written down");
    Check(deskhubp::CheckTrustedHost(viewerConfig.host.ToString(), identity.fingerprint) ==
              deskhub::TrustVerdict::Trusted,
        "so the next connection will not ask again");

    if (!savedCert.empty()) deskhubp::WriteAppDataFile(deskhubp::kHostCertFileName, savedCert);
    if (!savedKey.empty()) deskhubp::WriteAppDataFile(deskhubp::kHostKeyFileName, savedKey);
    if (savedTrust.empty())
        deskhubp::RemoveAppDataFile(deskhubp::kTrustStoreFileName);
    else
        deskhubp::WriteAppDataFile(deskhubp::kTrustStoreFileName, savedTrust);
}

void TestHostRefusesWithoutIdentity() {
    std::printf("[termhost] a host with no key of its own never offers a shell...\n");
    deskhubp::TerminalHost host;
    Check(!host.Start(deskhubp::TerminalHostConfig{}, deskhubp::HostIdentity{},
              deskhubp::TerminalHostCallbacks{}),
        "starting without an identity is refused");
    Check(!host.Running() && host.SessionCount() == 0, "and nothing is left running");
    host.Stop();
    Check(true, "stopping one that never started is safe");
}

}

void RunTerminalHostTests() {
    TestHostRefusesWithoutIdentity();
    TestHostSharesAShell();
    TestViewerTrustsThenRunsAShell();
}
