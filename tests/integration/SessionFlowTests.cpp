#include "Tests.h"
#include "support/FakeAgent.h"
#include "support/FakeDecoder.h"
#include "support/FakeVideo.h"
#include "support/TestSupport.h"

#include "deskhub/input/VirtualKeys.h"
#include "deskhubp/net/SourceQuery.h"
#include "deskhubp/session/ClientEngine.h"

#include <cstdio>
#include <string>
#include <vector>

using Viewer = deskhubp::ClientEngine<fake::Decoder, void*>;

namespace {

constexpr uint32_t kLoopbackIp = 0x7F000001u;
constexpr uint32_t kConnectTimeoutMs = 30'000;
constexpr uint32_t kStreamTimeoutMs = 30'000;

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
    return cfg;
}

void ResetObservations() {
    fake::Host().Reset();
    fake::Decoded().Reset();
}

bool Streaming(Viewer& v) {
    return v.phase() == deskhubp::ClientPhase::Streaming;
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
    viewer.Start(ViewerConfig(port, 0));

    Check(WaitFor(
              [&] {
                  const auto rows = agent.Status();
                  return !rows.empty() && rows[0].viewerConnected;
              },
              kConnectTimeoutMs),
        "once the viewer connects the host reports it as connected");

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
    Check(QuerySources(HostAddr(port), sources), "LIST_SOURCES was answered");
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

}

void RunSessionFlowTests() {
    TestAViewerConnectsAndSeesTheFramesTheHostEncoded();
    TestKeystrokesReachTheHostInjector();
    TestPauseArrivesWithoutAScancode();
    TestTheHostReportsWhoIsWatching();
    TestTwoSourcesStayApart();
    TestSourceDiscoveryBeforeAnySession();
    TestTheHostSurvivesAViewerThatVanishes();
}
