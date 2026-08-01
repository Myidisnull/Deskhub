#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/control/StreamSize.h"
#include "deskhub/session/SourcePipelineState.h"
#include "deskhubp/session/HostNetLoop.h"

#include <cstdio>
#include <memory>
#include <vector>

using deskhub::Feedback;
using deskhub::Hello;
using deskhub::InputEvent;
using deskhub::SourcePipelineState;
using deskhub::StreamSize;
using deskhubp::HostSessionHooks;
using deskhubp::MakeHostCallbacks;

namespace {

constexpr uint32_t kStartBps = 20'000'000;
constexpr uint32_t kMinBps = 1'000'000;

struct Recorder {
    std::vector<std::vector<uint8_t>> sent;
    std::vector<std::vector<uint8_t>> sentToPeer;
    std::vector<InputEvent> inputs;
    int releases = 0;
    int retargets = 0;
    int bitrateCalls = 0;
    int qualityCalls = 0;
    StreamSize target{1280, 720};

    HostSessionHooks Hooks() {
        HostSessionHooks h;
        h.fps = 60;
        h.send = [this](std::span<const uint8_t> d) { sent.emplace_back(d.begin(), d.end()); };
        h.sendToPeer = [this](std::span<const uint8_t> d) {
            sentToPeer.emplace_back(d.begin(), d.end());
        };
        h.retarget = [this] {
            ++retargets;
            return target;
        };
        h.applyInput = [this](const InputEvent& e) { inputs.push_back(e); };
        h.releaseInput = [this] { ++releases; };
        h.setEncoderBitrate = [this](uint32_t) {
            ++bitrateCalls;
            return true;
        };
        h.applyQualityStep = [this](const deskhub::QualityStep&, const deskhub::QualityStep&) {
            ++qualityCalls;
            return target;
        };
        return h;
    }
};

std::unique_ptr<SourcePipelineState> MakeSource(const char* name) {
    auto st = std::make_unique<SourcePipelineState>(kStartBps, kMinBps);
    st->name = name;
    st->sourceId = 1;
    st->srcW.store(1920);
    st->srcH.store(1080);
    return st;
}

Hello ClientHello(uint16_t w, uint16_t h) {
    Hello hello{};
    hello.clientId = 0x1234;
    hello.codecMask = deskhub::kCodecMaskH264;
    hello.maxWidth = w;
    hello.maxHeight = h;
    hello.desiredFps = 60;
    return hello;
}

void TestOutgoingBytesReachTheSocket() {
    std::printf("[hostcb] what the session wants to send is what the socket gets...\n");
    auto st = MakeSource("Display 1");
    Recorder rec;
    const auto cb = MakeHostCallbacks(*st, rec.Hooks());

    const uint8_t bytes[] = {1, 2, 3, 4};
    cb.send(bytes);
    Check(rec.sent.size() == 1, "one send in, one send out");
    Check(rec.sent[0] == std::vector<uint8_t>{1, 2, 3, 4}, "with the bytes unchanged");
}

void TestAMissingHookIsNotACrash() {
    std::printf("[hostcb] a client that wires up no hooks still gets a usable session...\n");
    auto st = MakeSource("Display 1");
    const auto cb = MakeHostCallbacks(*st, HostSessionHooks{});

    const uint8_t bytes[] = {9};
    cb.send(bytes);
    cb.onInput(InputEvent{});
    cb.onFocus(false);
    cb.onStart();
    cb.onKeyframeRequest();
    cb.onNack(1, std::span<const uint16_t>{});
    cb.onHello(ClientHello(1280, 720));
    cb.onFeedback(Feedback{});
    cb.onDisconnect();
    Check(true, "every callback tolerates the hook it depends on being absent");
}

void TestSessionIdsComeFromRealRandomness() {
    std::printf("[hostcb] the session id a host hands out is not guessable...\n");
    auto st = MakeSource("Display 1");
    Recorder rec;
    const auto cb = MakeHostCallbacks(*st, rec.Hooks());

    uint8_t buf[16];
    for (uint8_t& b : buf) b = 0;
    Check(cb.randomBytes(buf), "the platform RNG answers");

    bool allZero = true;
    for (uint8_t b : buf) {
        if (b) allZero = false;
    }
    Check(!allZero, "and it actually wrote something, rather than leaving zeros");
}

void TestTheFirstHelloBuildsTheOffer() {
    std::printf("[hostcb] the first HELLO decides the size and fps the host will send...\n");
    auto st = MakeSource("Display 1");
    Recorder rec;
    const auto cb = MakeHostCallbacks(*st, rec.Hooks());

    cb.onHello(ClientHello(1280, 720));

    Check(rec.retargets == 1, "the client size is resolved against the capture");
    Check(st->cliW == 1280 && st->cliH == 720, "and the client's screen is remembered");
    Check(st->ladder != nullptr, "a quality ladder is built for this client");
    Check(st->offer.width == 1280 && st->offer.height == 720,
        "the offer matches what the ladder was built from");
    Check(st->offer.fps != 0, "and it names an fps, so the client is not left guessing");
    Check(st->offer.bitrateBps == kStartBps, "the offer starts at the configured bitrate");
}

void TestAHelloWeCannotServeIsDeclined() {
    std::printf("[hostcb] a client we cannot size a stream for gets no offer at all...\n");
    auto st = MakeSource("Display 1");
    Recorder rec;
    rec.target = StreamSize{0, 0};
    const auto cb = MakeHostCallbacks(*st, rec.Hooks());

    cb.onHello(ClientHello(0, 0));
    Check(st->ladder == nullptr, "no ladder is built from a zero size");
    Check(st->offer.width == 0 && st->offer.height == 0,
        "and no offer is invented, so the client is rejected instead of sent garbage");
}

void TestStartAndKeyframeRequestsBothOweAnIdr() {
    std::printf("[hostcb] the first frame after START, or after a request, is a keyframe...\n");
    auto st = MakeSource("Display 1");
    Recorder rec;
    const auto cb = MakeHostCallbacks(*st, rec.Hooks());

    Check(!st->forceIdr.load(), "nothing is owed before the client asks");
    cb.onStart();
    Check(st->forceIdr.load(), "START owes a keyframe, or the client decodes nothing");

    st->forceIdr.store(false);
    cb.onKeyframeRequest();
    Check(st->forceIdr.load(), "an explicit request owes one too");
}

void TestInputIsHandedToTheInjector() {
    std::printf("[hostcb] a remote key press reaches the injector unchanged...\n");
    auto st = MakeSource("Display 1");
    Recorder rec;
    const auto cb = MakeHostCallbacks(*st, rec.Hooks());

    InputEvent e{};
    e.type = deskhub::InputType::Key;
    e.a = 'A';
    e.b = 0x1E;
    e.state = 1;
    cb.onInput(e);

    Check(rec.inputs.size() == 1, "the event is delivered");
    Check(rec.inputs[0].type == deskhub::InputType::Key && rec.inputs[0].a == 'A' &&
              rec.inputs[0].b == 0x1E && rec.inputs[0].state == 1,
        "with its key, scancode and up/down state intact");
}

void TestLosingFocusLetsGoOfEveryKey() {
    std::printf("[hostcb] a viewer that alt-tabs away does not leave keys stuck down...\n");
    auto st = MakeSource("Display 1");
    Recorder rec;
    const auto cb = MakeHostCallbacks(*st, rec.Hooks());

    cb.onFocus(true);
    Check(rec.releases == 0, "gaining focus releases nothing");

    cb.onFocus(false);
    Check(rec.releases == 1, "losing it releases everything the viewer was holding");
}

void TestADepartingClientIsForgottenCompletely() {
    std::printf("[hostcb] when the client leaves, nothing of it is kept around...\n");
    auto st = MakeSource("Display 1");
    Recorder rec;
    const auto cb = MakeHostCallbacks(*st, rec.Hooks());

    st->peerPacked.store(NetAddr{0x7F000001u, 40000}.Pack());
    cb.onDisconnect();

    Check(st->peerPacked.load() == 0,
        "the peer address is cleared, so nothing is sent to an address nobody is at");
    Check(rec.releases == 1, "and the keys it was holding are released on the host");
}

void TestANackForAnUnknownPeerSendsNothing() {
    std::printf("[hostcb] a NACK from nowhere does not make the host send anything...\n");
    auto st = MakeSource("Display 1");
    Recorder rec;
    const auto cb = MakeHostCallbacks(*st, rec.Hooks());

    const uint16_t indices[] = {0, 1, 2};
    cb.onNack(7, indices);
    Check(rec.sentToPeer.empty(), "with no peer adopted there is nobody to retransmit to");

    st->peerPacked.store(NetAddr{0x7F000001u, 40000}.Pack());
    cb.onNack(7, indices);
    Check(rec.sentToPeer.empty(), "and an empty retransmit cache has nothing to resend");
}

void TestFeedbackDrivesTheEncoder() {
    std::printf("[hostcb] the link report the client sends actually moves the encoder...\n");
    auto st = MakeSource("Display 1");
    Recorder rec;
    const auto cb = MakeHostCallbacks(*st, rec.Hooks());
    cb.onHello(ClientHello(1280, 720));

    Feedback fb{};
    fb.rttMs = 40;
    fb.lossPct = 20;
    fb.recvBitrateKbps = 3'000;
    cb.onFeedback(fb);

    Check(st->rate.bitrateBps() < kStartBps,
        "a lossy link pulls the bitrate down instead of insisting on the original");
    Check(rec.bitrateCalls >= 1, "and the encoder is told about it");
    Check(st->wantFec.load(), "20% loss turns FEC on");
}

void TestAHealthyLinkIsLeftAlone() {
    std::printf("[hostcb] a clean link is not disturbed by the first report...\n");
    auto st = MakeSource("Display 1");
    Recorder rec;
    const auto cb = MakeHostCallbacks(*st, rec.Hooks());
    cb.onHello(ClientHello(1280, 720));

    Feedback fb{};
    fb.rttMs = 5;
    fb.lossPct = 0;
    fb.recvBitrateKbps = 19'000;
    cb.onFeedback(fb);

    Check(st->rate.bitrateBps() == kStartBps, "no loss, no reason to change the bitrate");
    Check(!st->wantFec.load(), "and no reason to pay for FEC");
    Check(st->uiRttMs.load() == 5, "the UI still learns the measured round trip");
    Check(st->haveFeedback.load(), "and knows the numbers are real, not placeholders");
}

void TestEachSourceKeepsItsOwnState() {
    std::printf("[hostcb] two shared displays never write into each other's state...\n");
    auto a = MakeSource("Display 1");
    auto b = MakeSource("Display 2");
    Recorder recA, recB;
    const auto cbA = MakeHostCallbacks(*a, recA.Hooks());
    const auto cbB = MakeHostCallbacks(*b, recB.Hooks());

    cbA.onStart();
    Check(a->forceIdr.load(), "the source that was started owes a keyframe");
    Check(!b->forceIdr.load(), "the other one is untouched");

    cbB.onHello(ClientHello(800, 600));
    Check(b->cliW == 800 && a->cliW == 0, "and a HELLO only reaches the source it was for");
    Check(recA.retargets == 0 && recB.retargets == 1, "the hooks stay with their own source");
}

}

void RunHostCallbackTests() {
    TestOutgoingBytesReachTheSocket();
    TestAMissingHookIsNotACrash();
    TestSessionIdsComeFromRealRandomness();
    TestTheFirstHelloBuildsTheOffer();
    TestAHelloWeCannotServeIsDeclined();
    TestStartAndKeyframeRequestsBothOweAnIdr();
    TestInputIsHandedToTheInjector();
    TestLosingFocusLetsGoOfEveryKey();
    TestADepartingClientIsForgottenCompletely();
    TestANackForAnUnknownPeerSendsNothing();
    TestFeedbackDrivesTheEncoder();
    TestAHealthyLinkIsLeftAlone();
    TestEachSourceKeepsItsOwnState();
}
