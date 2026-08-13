#include "Tests.h"
#include "support/FakeAgent.h"
#include "support/FakeDecoder.h"
#include "support/FakeVideo.h"
#include "support/TestSupport.h"

#include "deskhub/input/VirtualKeys.h"
#include "deskhubp/net/SourceQuery.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/session/ClientEngine.h"

#include <cstdio>
#include <string>
#include <vector>

using Viewer = deskhubp::ClientEngine<fake::Decoder, void*>;

namespace {

constexpr uint32_t kLoopbackIp = 0x7F000001u;
constexpr uint32_t kConnectTimeoutMs = 30'000;
constexpr uint32_t kStreamTimeoutMs = 30'000;
constexpr uint32_t kClashWindowMs = 750;

void* const kDummySurface = reinterpret_cast<void*>(0x1);

NetAddr HostAddr(uint16_t port) {
    return NetAddr{kLoopbackIp, port};
}

deskhubp::ClientEngineConfig ViewerConfig(uint16_t port, uint8_t sourceId) {
    deskhubp::ClientEngineConfig cfg;
    cfg.server = HostAddr(port);
    cfg.sourceId = sourceId;
    cfg.screenW = 1920;
    cfg.screenH = 1080;
    cfg.desiredFps = 30;
    cfg.alwaysFocused = true;
    cfg.passcode = kTestPasscode;
    return cfg;
}

void ResetObservations() {
    fake::Host().Reset();
    fake::Decoded().Reset();
}

bool Streaming(Viewer& v) {
    return v.phase() == deskhubp::ClientPhase::Streaming;
}

bool SawKey(const std::vector<deskhub::InputEvent>& inputs, int32_t virtualKey) {
    for (const deskhub::InputEvent& e : inputs)
        if (e.type == deskhub::InputType::Key && e.a == virtualKey) return true;
    return false;
}

void TestAViewerConnectsAndSeesTheFramesTheHostEncoded() {
    std::printf("[e2e] a viewer connects over loopback and decodes what the host sent...\n");
    ResetObservations();
    const uint16_t port = NextTestPort();

    fake::Agent agent;
    if (!agent.Start({fake::Source("Display 1", 1280, 720, 1)}, port)) {
        Check(false, "the host could not start");
        std::printf("  host error: %s\n", agent.LastError().c_str());
        return;
    }

    Viewer viewer;
    viewer.SetSurface(kDummySurface);
    Check(viewer.Start(ViewerConfig(port, 0)), "the viewer opened its socket");

    Check(WaitFor([&] { return Streaming(viewer); }, kConnectTimeoutMs),
        "HELLO / HELLO_ACK / START completed and the session reached Streaming");

    Check(WaitFor([&] { return fake::Decoded().frameCount() >= 3; }, kStreamTimeoutMs),
        "encoded frames crossed the wire, were reassembled and reached the decoder");

    Check(fake::Decoded().inits.load() >= 1, "the decoder was opened for the negotiated size");
    Check(viewer.videoWidth() != 0 && viewer.videoHeight() != 0,
        "the viewer learned the size it is receiving");
    Check(fake::Decoded().width.load() == int(viewer.videoWidth()) &&
              fake::Decoded().height.load() == int(viewer.videoHeight()),
        "and opened the decoder at exactly that size");

    const auto frames = fake::Decoded().TakeFrames();
    uint32_t previous = 0;
    bool first = true;
    for (const auto& f : frames) {
        uint32_t index = 0;
        if (!fake::FrameIndexOf(f, index)) {
            Check(false, "every decoded frame is byte-for-byte what the encoder produced");
            break;
        }
        if (!first) Check(index > previous, "frames arrive in order, none replayed");
        previous = index;
        first = false;
    }

    viewer.Stop();
    agent.Stop();
}

void TestKeystrokesReachTheHostInjector() {
    std::printf("[e2e] a key pressed in the viewer is injected on the host...\n");
    ResetObservations();
    const uint16_t port = NextTestPort();

    fake::Agent agent;
    if (!agent.Start({fake::Source("Display 1", 1280, 720, 1)}, port)) {
        Check(false, "the host could not start");
        return;
    }

    Viewer viewer;
    viewer.SetSurface(kDummySurface);
    viewer.Start(ViewerConfig(port, 0));
    if (!WaitFor([&] { return Streaming(viewer); }, kConnectTimeoutMs)) {
        Check(false, "the session never reached Streaming");
        viewer.Stop();
        agent.Stop();
        return;
    }

    viewer.QueueKey('A', 0x1E, true);
    viewer.QueueKey('A', 0x1E, false);
    viewer.QueueMouseButton(int32_t(deskhub::MouseButton::Left), true);
    viewer.QueueMouseButton(int32_t(deskhub::MouseButton::Left), false);
    viewer.QueueMouseWheel(-120);

    Check(WaitFor([&] { return fake::Host().inputCount() >= 5; }, kStreamTimeoutMs),
        "all five events were delivered to the host injector");

    const auto inputs = fake::Host().TakeInputs();
    if (inputs.size() >= 5) {
        Check(inputs[0].type == deskhub::InputType::Key && inputs[0].a == 'A' &&
                  inputs[0].b == 0x1E && inputs[0].state == 1,
            "the key press keeps its virtual key, scancode and state across the wire");
        Check(inputs[1].type == deskhub::InputType::Key && inputs[1].state == 0,
            "and so does the release");
        Check(inputs[2].type == deskhub::InputType::MouseButton && inputs[2].state == 1,
            "the button press arrives as a button, not as a key");
        Check(inputs[4].type == deskhub::InputType::MouseWheel && inputs[4].b == -120,
            "the wheel delta keeps its sign, so scrolling does not invert");
    }

    viewer.Stop();
    agent.Stop();
}

void TestPauseArrivesWithoutAScancode() {
    std::printf("[e2e] Pause reaches the host as a virtual key with no scancode...\n");
    ResetObservations();
    const uint16_t port = NextTestPort();

    fake::Agent agent;
    if (!agent.Start({fake::Source("Display 1", 1280, 720, 1)}, port)) {
        Check(false, "the host could not start");
        return;
    }

    Viewer viewer;
    viewer.SetSurface(kDummySurface);
    viewer.Start(ViewerConfig(port, 0));
    if (!WaitFor([&] { return Streaming(viewer); }, kConnectTimeoutMs)) {
        Check(false, "the session never reached Streaming");
        viewer.Stop();
        agent.Stop();
        return;
    }

    viewer.QueueKey(deskhub::kVkPause, 0x45, true);
    Check(WaitFor([&] { return fake::Host().inputCount() >= 1; }, kStreamTimeoutMs),
        "the Pause press was delivered");

    const auto inputs = fake::Host().TakeInputs();
    if (!inputs.empty())
        Check(inputs[0].a == deskhub::kVkPause && inputs[0].b == 0,
            "the host is told which key it is, not a scancode that means NumLock");

    viewer.Stop();
    agent.Stop();
}

void TestTheHostReportsWhoIsWatching() {
    std::printf("[e2e] the sharing UI sees a viewer arrive and leave...\n");
    ResetObservations();
    const uint16_t port = NextTestPort();

    fake::Agent agent;
    if (!agent.Start({fake::Source("Display 1", 1280, 720, 1)}, port)) {
        Check(false, "the host could not start");
        return;
    }

    const auto idle = agent.Status();
    Check(idle.size() == 1, "one shared source is listed");
    if (!idle.empty()) {
        Check(!idle[0].viewerConnected, "with nobody watching it yet");
        Check(idle[0].name == "Display 1", "under the name it was shared with");
    }

    Viewer viewer;
    viewer.SetSurface(kDummySurface);
    deskhubp::ClientEngineConfig named = ViewerConfig(port, 0);
    named.displayName = "Anh's laptop";
    viewer.Start(named);

    Check(WaitFor(
              [&] {
                  const auto rows = agent.Status();
                  return !rows.empty() && rows[0].viewerConnected;
              },
              kConnectTimeoutMs),
        "once the viewer connects the host reports it as connected");

    const auto watching = agent.Status();
    Check(!watching.empty() && watching[0].viewerNames.size() == 1 &&
              watching[0].viewerNames[0] == "Anh's laptop",
        "and shows the name the viewer chose for itself");
    Check(!watching.empty() && !watching[0].viewerAddr.empty() &&
              watching[0].viewerAddr.find("Anh's laptop (") == 0,
        "the status line leads with that name");

    viewer.Stop();

    Check(WaitFor(
              [&] {
                  const auto rows = agent.Status();
                  return !rows.empty() && !rows[0].viewerConnected;
              },
              kStreamTimeoutMs),
        "and after BYE the host reports the source as free again");

    agent.Stop();
}

void TestTwoSourcesStayApart() {
    std::printf("[e2e] with two displays shared, a viewer gets the one it asked for...\n");
    ResetObservations();
    const uint16_t port = NextTestPort();

    fake::Agent agent;
    const bool started = agent.Start(
        {fake::Source("Display 1", 1280, 720, 1), fake::Source("Display 2", 800, 600, 2)}, port);
    if (!started) {
        Check(false, "the host could not start");
        return;
    }

    const auto rows = agent.Status();
    Check(rows.size() == 2, "both sources are shared");

    Viewer viewer;
    viewer.SetSurface(kDummySurface);
    viewer.Start(ViewerConfig(port, 1));

    Check(WaitFor([&] { return Streaming(viewer); }, kConnectTimeoutMs),
        "the viewer negotiated with source 1");
    Check(WaitFor([&] { return fake::Decoded().frameCount() >= 2; }, kStreamTimeoutMs),
        "and receives video");

    Check(viewer.videoWidth() <= 800 && viewer.videoHeight() <= 600,
        "the stream is sized from Display 2, not from the larger Display 1");

    Check(WaitFor(
              [&] {
                  const auto live = agent.Status();
                  return live.size() == 2 && live[1].viewerConnected && !live[0].viewerConnected;
              },
              kStreamTimeoutMs),
        "only the source that was asked for reports a viewer");

    viewer.Stop();
    agent.Stop();
}

void TestOneMachineWatchesBothDisplaysAtOnce() {
    std::printf("[e2e] one machine opens a window per display of a two-display host...\n");
    ResetObservations();
    const uint16_t port = NextTestPort();

    fake::Agent agent;
    const bool started = agent.Start(
        {fake::Source("Display 1", 1280, 720, 1), fake::Source("Display 2", 800, 600, 2)}, port);
    if (!started) {
        Check(false, "the host could not start");
        std::printf("  host error: %s\n", agent.LastError().c_str());
        return;
    }

    Viewer firstDisplay;
    firstDisplay.SetSurface(kDummySurface);
    Check(firstDisplay.Start(ViewerConfig(port, 0)), "the first window opened its socket");
    Check(WaitFor([&] { return Streaming(firstDisplay); }, kConnectTimeoutMs),
        "the window watching Display 1 reaches Streaming");

    Viewer secondDisplay;
    secondDisplay.SetSurface(kDummySurface);
    Check(secondDisplay.Start(ViewerConfig(port, 1)), "the second window opened its socket");
    Check(WaitFor([&] { return Streaming(secondDisplay); }, kConnectTimeoutMs),
        "the window watching Display 2 reaches Streaming too, instead of being turned away");

    Check(WaitFor([&] { return Streaming(firstDisplay); }, kClashWindowMs),
        "and the first window is still streaming, not knocked off by the second");

    Check(WaitFor(
              [&] {
                  const auto rows = agent.Status();
                  return rows.size() == 2 && rows[0].viewerConnected && rows[1].viewerConnected;
              },
              kStreamTimeoutMs),
        "the host reports a viewer on both displays");

    Check(WaitFor([&] { return fake::Decoded().frameCount() >= 6; }, kStreamTimeoutMs),
        "both windows keep receiving video");

    Check(firstDisplay.videoWidth() > secondDisplay.videoWidth(),
        "each window is sized from its own display, not from a single shared stream");

    firstDisplay.Stop();
    Check(WaitFor([&] { return Streaming(secondDisplay); }, kClashWindowMs),
        "closing one window leaves the other one streaming");

    secondDisplay.Stop();
    agent.Stop();
}

void TestSourceDiscoveryBeforeAnySession() {
    std::printf("[e2e] a viewer can ask what is shared before opening a session...\n");
    ResetObservations();
    const uint16_t port = NextTestPort();

    fake::Agent agent;
    const bool started = agent.Start(
        {fake::Source("Display 1", 1280, 720, 1), fake::Source("Display 2", 800, 600, 2)}, port);
    if (!started) {
        Check(false, "the host could not start");
        return;
    }

    std::vector<deskhub::SourceInfo> sources;
    Check(QuerySources(HostAddr(port), sources, kTestPasscode), "LIST_SOURCES was answered");
    Check(sources.size() == 2, "both shared sources come back");
    if (sources.size() == 2) {
        Check(sources[0].name == "Display 1" && sources[1].name == "Display 2",
            "with the names the host is sharing them under");
        Check(sources[0].sourceId != sources[1].sourceId,
            "and distinct ids, so the picker can start the right one");
        Check(sources[0].width == 1280 && sources[0].height == 720,
            "the picker is told the size it would be watching");
    }

    agent.Stop();
}

void TestTwoViewersShareOneSourceAndOneEncoder() {
    std::printf("[e2e] two viewers watch the same source off a single encode...\n");
    ResetObservations();
    const uint16_t port = NextTestPort();

    fake::Agent agent;
    if (!agent.Start({fake::Source("Display 1", 1280, 720, 1)}, port)) {
        Check(false, "the host could not start");
        return;
    }

    Viewer first;
    first.SetSurface(kDummySurface);
    first.Start(ViewerConfig(port, 0));
    if (!WaitFor([&] { return Streaming(first); }, kConnectTimeoutMs)) {
        Check(false, "the first viewer never reached Streaming");
        first.Stop();
        agent.Stop();
        return;
    }

    Viewer second;
    second.SetSurface(kDummySurface);
    second.Start(ViewerConfig(port, 0));
    Check(WaitFor([&] { return Streaming(second); }, kConnectTimeoutMs),
        "the second viewer is let in rather than turned away as busy");

    Check(WaitFor(
              [&] {
                  const auto rows = agent.Status();
                  return !rows.empty() && rows[0].viewerCount == 2;
              },
              kStreamTimeoutMs),
        "and the sharing UI counts both of them");

    const uint32_t encodedBefore = fake::Host().framesEncoded.load();
    Check(WaitFor([&] { return fake::Decoded().frameCount() >= 6; }, kStreamTimeoutMs),
        "both viewers decode video");
    const uint32_t encoded = fake::Host().framesEncoded.load() - encodedBefore;
    Check(encoded < fake::Decoded().frameCount(),
        "the host encoded fewer frames than were decoded, so one encode fed both viewers");

    second.QueueKey('B', 0x30, true);
    second.QueueKey('B', 0x30, false);
    Check(WaitFor([&] { return SawKey(fake::Host().TakeInputs(), 'B'); }, kStreamTimeoutMs),
        "with the other viewer idle, the second one drives the host");

    first.QueueKey('A', 0x1E, true);
    first.QueueKey('A', 0x1E, false);
    Check(WaitFor([&] { return SawKey(fake::Host().TakeInputs(), 'A'); }, kStreamTimeoutMs),
        "the viewer that connected first can cut in at any time");

    fake::Host().TakeInputs();
    second.QueueKey('D', 0x20, true);
    second.QueueKey('D', 0x20, false);
    Check(!WaitFor([&] { return SawKey(fake::Host().TakeInputs(), 'D'); }, kClashWindowMs),
        "and while it is driving, the later viewer loses the clash");

    first.Stop();
    Check(WaitFor(
              [&] {
                  const auto rows = agent.Status();
                  return !rows.empty() && rows[0].viewerCount == 1;
              },
              kStreamTimeoutMs),
        "when the first viewer leaves the count drops");
    Check(Streaming(second), "and the remaining viewer keeps streaming");

    fake::Host().TakeInputs();
    second.QueueKey('C', 0x2E, true);
    second.QueueKey('C', 0x2E, false);
    Check(WaitFor([&] { return SawKey(fake::Host().TakeInputs(), 'C'); }, kStreamTimeoutMs),
        "control passes to it, so its keys now reach the host");

    second.Stop();
    agent.Stop();
}

void TestTheHostSurvivesAViewerThatVanishes() {
    std::printf("[e2e] a viewer that disappears without a BYE does not wedge the host...\n");
    ResetObservations();
    const uint16_t port = NextTestPort();

    fake::Agent agent;
    if (!agent.Start({fake::Source("Display 1", 1280, 720, 1)}, port)) {
        Check(false, "the host could not start");
        return;
    }

    {
        Viewer viewer;
        viewer.SetSurface(kDummySurface);
        viewer.Start(ViewerConfig(port, 0));
        Check(WaitFor([&] { return Streaming(viewer); }, kConnectTimeoutMs),
            "the first viewer streams");
        viewer.Stop();
    }

    Viewer second;
    second.SetSurface(kDummySurface);
    second.Start(ViewerConfig(port, 0));
    Check(WaitFor([&] { return Streaming(second); }, kConnectTimeoutMs),
        "a second viewer can take over the same source afterwards");
    Check(WaitFor([&] { return fake::Decoded().frameCount() >= 2; }, kStreamTimeoutMs),
        "and receives video of its own");

    second.Stop();
    agent.Stop();
    Check(!agent.running(), "and the host shuts down cleanly");
}

void TestPasscodeGatesTheStream() {
    std::printf("[e2e] a host with a passcode only streams to viewers that know it...\n");
    ResetObservations();
    const uint16_t port = NextTestPort();

    fake::Agent agent;
    if (!agent.Start({fake::Source("Display 1", 1280, 720, 1)}, port, 30, 1920, "4726")) {
        Check(false, "the host could not start");
        std::printf("  host error: %s\n", agent.LastError().c_str());
        return;
    }

    {
        Viewer wrong;
        wrong.SetSurface(kDummySurface);
        deskhubp::ClientEngineConfig cfg = ViewerConfig(port, 0);
        cfg.passcode = "1111";
        wrong.Start(cfg);

        Check(WaitFor([&] { return wrong.phase() == deskhubp::ClientPhase::Ended; },
                  kConnectTimeoutMs),
            "a viewer with the wrong passcode is turned away instead of hanging");
        Check(wrong.EndReason().find("passcode") != std::string::npos,
            "and is told the passcode was the problem");
        Check(fake::Decoded().frameCount() == 0, "not one frame of the screen leaked to it");
        wrong.Stop();
    }

    {
        Viewer blank;
        blank.SetSurface(kDummySurface);
        deskhubp::ClientEngineConfig cfg = ViewerConfig(port, 0);
        cfg.passcode.clear();
        blank.Start(cfg);
        Check(WaitFor([&] { return blank.phase() == deskhubp::ClientPhase::Ended; },
                  kConnectTimeoutMs),
            "so is a viewer that sends no passcode at all");
        Check(fake::Decoded().frameCount() == 0, "still nothing decoded");
        blank.Stop();
    }

    ResetObservations();
    Viewer right;
    right.SetSurface(kDummySurface);
    deskhubp::ClientEngineConfig cfg = ViewerConfig(port, 0);
    cfg.passcode = "4726";
    right.Start(cfg);

    Check(WaitFor([&] { return Streaming(right); }, kConnectTimeoutMs),
        "the viewer with the right passcode is let in");
    Check(WaitFor([&] { return fake::Decoded().frameCount() >= 3; }, kStreamTimeoutMs),
        "and receives real video once past the gate");

    right.Stop();
    agent.Stop();
}

void TestDiscoveryIsGatedByThePasscode() {
    std::printf("[e2e] a protected host answers discovery but names no displays...\n");
    ResetObservations();
    const uint16_t port = NextTestPort();

    fake::Agent agent;
    if (!agent.Start({fake::Source("DELL U2723QE", 1280, 720, 1)}, port, 30, 1920, "4726")) {
        Check(false, "the host could not start");
        return;
    }

    std::vector<deskhub::SourceInfo> sources;
    Check(QuerySources(HostAddr(port), sources),
        "the host still replies, so it shows as online in the device list");
    Check(sources.empty(), "but a viewer without the passcode sees no display names or sizes");

    Check(QuerySources(HostAddr(port), sources, "1111") && sources.empty(),
        "a wrong passcode learns nothing either");

    Check(QuerySources(HostAddr(port), sources, "4726"), "the right passcode is answered");
    Check(sources.size() == 1 && sources[0].name == "DELL U2723QE",
        "and gets the real display list");

    agent.Stop();
}

void TestAHostWithoutAPasscodeServesNobody() {
    std::printf("[e2e] a host left without a passcode turns every viewer away...\n");
    ResetObservations();
    const uint16_t port = NextTestPort();

    fake::Agent agent;
    if (!agent.Start({fake::Source("Display 1", 1280, 720, 1)}, port, 30, 1920, "")) {
        Check(false, "the host could not start");
        return;
    }

    Viewer viewer;
    viewer.SetSurface(kDummySurface);
    deskhubp::ClientEngineConfig cfg = ViewerConfig(port, 0);
    cfg.passcode = "0000";
    viewer.Start(cfg);

    Check(WaitFor([&] { return viewer.phase() == deskhubp::ClientPhase::Ended; },
              kConnectTimeoutMs),
        "no passcode on the host means no stream, whatever the viewer sends");
    Check(fake::Decoded().frameCount() == 0, "and nothing of the screen leaves the machine");

    std::vector<deskhub::SourceInfo> sources;
    Check(QuerySources(HostAddr(port), sources, "0000") && sources.empty(),
        "discovery gives up nothing either");

    viewer.Stop();
    agent.Stop();
}

void TestJunkDatagramsDoNotDisturbTheStream() {
    std::printf("[e2e] junk sprayed at the host's port does not disturb the stream...\n");
    ResetObservations();
    const uint16_t port = NextTestPort();

    fake::Agent agent;
    if (!agent.Start({fake::Source("Display 1", 1280, 720, 1)}, port)) {
        Check(false, "the host could not start");
        return;
    }

    Viewer viewer;
    viewer.SetSurface(kDummySurface);
    Check(viewer.Start(ViewerConfig(port, 0)), "the viewer opened its socket");
    Check(WaitFor([&] { return Streaming(viewer); }, kConnectTimeoutMs),
        "the session reached Streaming");
    Check(WaitFor([&] { return fake::Decoded().frameCount() >= 2; }, kStreamTimeoutMs),
        "frames were flowing before the noise started");

    UdpSocket attacker;
    Check(attacker.Open(0), "the attacker opened its own socket");
    const NetAddr hostAddr = HostAddr(port);
    uint32_t seed = 0x1234567u;
    const auto rnd = [&seed] {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };
    uint8_t pkt[deskhub::kMaxDatagram];
    for (int i = 0; i < 400; ++i) {
        const size_t len = 1 + rnd() % sizeof(pkt);
        for (size_t k = 0; k < len; ++k) pkt[k] = uint8_t(rnd());
        attacker.SendTo(hostAddr, pkt, len);
    }
    for (int i = 0; i < 50; ++i) {
        const size_t byeLen = deskhub::BuildBye(pkt, rnd());
        attacker.SendTo(hostAddr, pkt, byeLen);
        const uint16_t indices[2] = {uint16_t(rnd()), uint16_t(rnd())};
        const size_t nackLen = deskhub::BuildNack(pkt, rnd(), rnd(), indices);
        attacker.SendTo(hostAddr, pkt, nackLen);
    }

    const size_t before = fake::Decoded().frameCount();
    Check(WaitFor([&] { return fake::Decoded().frameCount() >= before + 3; }, kStreamTimeoutMs),
        "the viewer keeps decoding fresh frames through the noise");
    Check(Streaming(viewer), "the session is still Streaming");
    Check(agent.running(), "the host is still running");

    viewer.Stop();
    agent.Stop();
}

}

void RunSessionFlowTests() {
    TestAViewerConnectsAndSeesTheFramesTheHostEncoded();
    TestPasscodeGatesTheStream();
    TestDiscoveryIsGatedByThePasscode();
    TestAHostWithoutAPasscodeServesNobody();
    TestKeystrokesReachTheHostInjector();
    TestPauseArrivesWithoutAScancode();
    TestTheHostReportsWhoIsWatching();
    TestTwoSourcesStayApart();
    TestOneMachineWatchesBothDisplaysAtOnce();
    TestTwoViewersShareOneSourceAndOneEncoder();
    TestSourceDiscoveryBeforeAnySession();
    TestTheHostSurvivesAViewerThatVanishes();
    TestJunkDatagramsDoNotDisturbTheStream();
}
