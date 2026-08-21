#include "Tests.h"
#include "support/FakeAgent.h"
#include "support/FakeDecoder.h"
#include "support/FakeVideo.h"
#include "support/TestSupport.h"

#include "deskhubp/session/ClientEngine.h"
#include "deskhubp/session/FileHost.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/system/PairedDevicesFile.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

using Viewer = deskhubp::ClientEngine<fake::Decoder, void*>;

namespace {

constexpr uint32_t kLoopbackIp = 0x7F000001u;
constexpr uint32_t kConnectTimeoutMs = 30'000;
constexpr uint32_t kTransferTimeoutMs = 120'000;
constexpr uint64_t kBaselineWindowUs = 1'000'000;
constexpr uint64_t kSampleUs = 20'000;
constexpr size_t kTransferBytes = 32u << 20;
constexpr uint64_t kWorstGapMs = 1500;

void* const kDummySurface = reinterpret_cast<void*>(0x1);

std::filesystem::path Scratch(const std::string& leaf) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "deskhub-transfer-load";
    std::error_code ec;
    std::filesystem::remove_all(root / leaf, ec);
    std::filesystem::create_directories(root / leaf, ec);
    return root / leaf;
}

std::vector<uint8_t> Pattern(size_t size) {
    std::vector<uint8_t> bytes(size);
    for (size_t i = 0; i < size; ++i) bytes[i] = uint8_t(i * 131u + (i >> 13));
    return bytes;
}

std::filesystem::path WriteBytes(const std::filesystem::path& dir, const std::string& name,
    const std::vector<uint8_t>& bytes) {
    const std::filesystem::path path = dir / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    return path;
}

std::vector<uint8_t> ReadBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}

deskhubp::ClientEngineConfig ViewerConfig(uint16_t port, bool wantsAudio) {
    deskhubp::ClientEngineConfig cfg;
    cfg.server = NetAddr{kLoopbackIp, port};
    cfg.sourceId = 0;
    cfg.screenW = 1920;
    cfg.screenH = 1080;
    cfg.desiredFps = 30;
    cfg.alwaysFocused = true;
    cfg.passcode = kTestPasscode;
    cfg.wantsAudio = wantsAudio;
    return cfg;
}

class Continuity {
public:
    explicit Continuity(std::function<uint64_t()> count) : count_(std::move(count)) {}

    void Begin() {
        first_ = count_();
        last_ = first_;
        startUs_ = NowUs();
        lastMoveUs_ = startUs_;
        worstGapUs_ = 0;
    }

    void Sample() {
        const uint64_t seen = count_();
        const uint64_t now = NowUs();
        if (seen != last_) {
            last_ = seen;
            lastMoveUs_ = now;
            return;
        }
        if (now - lastMoveUs_ > worstGapUs_) worstGapUs_ = now - lastMoveUs_;
    }

    uint64_t moved() const {
        return last_ - first_;
    }

    uint64_t worstGapMs() const {
        return worstGapUs_ / 1000;
    }

    double perSecond() const {
        const uint64_t spentUs = NowUs() - startUs_;
        return spentUs == 0 ? 0.0 : double(moved()) * 1'000'000.0 / double(spentUs);
    }

private:
    std::function<uint64_t()> count_;
    uint64_t first_ = 0;
    uint64_t last_ = 0;
    uint64_t startUs_ = 0;
    uint64_t lastMoveUs_ = 0;
    uint64_t worstGapUs_ = 0;
};

uint64_t DecodedFrames() {
    return uint64_t(fake::Decoded().frameCount());
}

struct Session {
    fake::Agent agent{};
    deskhubp::FileHost files{};
    Viewer viewer{};
    std::filesystem::path landing{};
    std::filesystem::path source{};
    std::vector<uint8_t> payload{};
    std::filesystem::path file{};

    ~Session() {
        viewer.Stop();
        files.Stop();
        agent.Stop();
    }

    bool Open(const std::string& leaf, bool audio) {
        fake::Host().Reset();
        fake::Decoded().Reset();
        deskhubp::ForgetAllPairedDevices();

        landing = Scratch(leaf + "-land");
        source = Scratch(leaf + "-send");
        payload = Pattern(kTransferBytes);
        file = WriteBytes(source, "bulk.bin", payload);

        const uint16_t port = NextTestPort();
        if (!agent.Start({fake::Source("Display 1", 1280, 720, 1)}, port, 30, 1920,
                kTestPasscode, true, audio)) {
            Check(false, "the host could not start");
            return false;
        }
        if (!files.Start(agent.socket(), landing, deskhubp::FileHostCallbacks{})) {
            Check(false, "the host could not take files in");
            return false;
        }
        agent.SetFiles(&files);

        deskhubp::ClientEngineConfig cfg = ViewerConfig(port, audio);
        cfg.onTrustAsked = [this](deskhub::TrustVerdict, std::string_view) {
            viewer.AcceptFingerprint();
        };
        viewer.SetSurface(kDummySurface);
        if (!viewer.Start(cfg)) {
            Check(false, "the viewer could not open its socket");
            return false;
        }
        if (!WaitFor([this] { return viewer.phase() == deskhubp::ClientPhase::Streaming; },
                kConnectTimeoutMs)) {
            Check(false, "the session never reached Streaming");
            return false;
        }
        return WaitFor([] { return DecodedFrames() >= 5; }, kConnectTimeoutMs);
    }

    bool LandedWhole() const {
        return ReadBytes(landing / "bulk.bin") == payload;
    }
};

void TestABigTransferNeverStallsTheStream() {
    std::printf("[load] a 32 MB upload rides alongside the video without stalling it...\n");

    Session s;
    if (!s.Open("stream", false)) return;

    Continuity idle(DecodedFrames);
    idle.Begin();
    for (uint64_t spent = 0; spent < kBaselineWindowUs; spent += kSampleUs) {
        SleepUs(kSampleUs);
        idle.Sample();
    }
    const double idleFps = idle.perSecond();
    const uint64_t idleGapMs = idle.worstGapMs();

    Check(idleFps > 1.0, "the stream is genuinely running before the transfer starts");

    Continuity busy(DecodedFrames);
    busy.Begin();
    const uint64_t startedUs = NowUs();
    Check(s.viewer.SendFiles({s.file}), "the viewer offers the batch mid-session");

    bool finished = false;
    size_t deepestLane = 0;
    for (uint32_t waited = 0; waited < kTransferTimeoutMs * 1000 / kSampleUs; ++waited) {
        SleepUs(kSampleUs);
        busy.Sample();
        const size_t depth = s.agent.socket().BulkQueued();
        if (depth > deepestLane) deepestLane = depth;
        if (!s.viewer.uploading()) {
            finished = true;
            break;
        }
    }
    const uint64_t spentMs = (NowUs() - startedUs) / 1000;
    const double busyFps = busy.perSecond();

    std::printf(
        "        idle %.1f fps (worst gap %llu ms) -> busy %.1f fps (worst gap %llu ms), "
        "32 MB in %llu ms, deepest file lane %zu\n",
        idleFps, static_cast<unsigned long long>(idleGapMs), busyFps,
        static_cast<unsigned long long>(busy.worstGapMs()),
        static_cast<unsigned long long>(spentMs), deepestLane);

    Check(finished, "the transfer settles inside the deadline");
    Check(s.viewer.uploadState() == deskhub::FileSenderState::Done, "and settles as done");
    Check(s.LandedWhole(), "every byte of the file reached the host, unchanged");

    Check(busy.moved() > 0, "frames kept arriving while the file was crossing");
    Check(busy.worstGapMs() < kWorstGapMs,
        "and never went dark for anything like the seconds a file backlog sharing their "
        "receive queue used to cost them");
    Check(busyFps > idleFps / 3.0,
        "the frame rate under a 32 MB upload stays in the same league as the idle rate");
    Check(deepestLane <= deskhubp::kMaxBulkQueued,
        "and the host's file lane stayed inside its cap the whole way, so 32 MB arriving "
        "faster than it can be written never becomes 32 MB of queue");
}

void TestInputStaysLiveDuringABigTransfer() {
    std::printf("[load] keystrokes still reach the host with an upload in flight...\n");

    Session s;
    if (!s.Open("input", false)) return;

    Check(s.viewer.SendFiles({s.file}), "the batch is offered");
    Check(WaitFor([&s] { return s.viewer.uploadProgress().batchBytes > kTransferBytes / 8; },
              kTransferTimeoutMs),
        "the transfer is well under way");

    const bool stillRunning = s.viewer.uploading();
    fake::Host().Reset();

    const uint64_t sentUs = NowUs();
    s.viewer.QueueKey('A', 0x1E, true);
    s.viewer.QueueKey('A', 0x1E, false);
    s.viewer.QueueMouseButton(int32_t(deskhub::MouseButton::Left), true);
    s.viewer.QueueMouseButton(int32_t(deskhub::MouseButton::Left), false);
    s.viewer.QueueMouseWheel(-120);

    const bool arrived = WaitFor([] { return fake::Host().inputCount() >= 5; }, 5000);
    const uint64_t latencyMs = (NowUs() - sentUs) / 1000;

    std::printf("        five events injected %llu ms after they were queued\n",
        static_cast<unsigned long long>(latencyMs));

    Check(stillRunning, "the upload really was in flight when the keys were pressed");
    Check(arrived, "all five events reached the host injector");
    Check(latencyMs < 2000,
        "and did so promptly, so input is not queued behind however many file chunks are "
        "in front of it");

    Check(WaitFor([&s] { return !s.viewer.uploading(); }, kTransferTimeoutMs),
        "the transfer still finishes");
    Check(s.viewer.uploadState() == deskhub::FileSenderState::Done, "as done");
    Check(s.LandedWhole(), "with the file intact");
}

void TestAudioKeepsFlowingDuringABigTransfer() {
    std::printf("[load] audio keeps arriving with an upload in flight...\n");

    Session s;
    if (!s.Open("audio", true)) return;

    if (!s.viewer.audioRunning()) {
        std::printf("[load] skipped: this machine opened no audio output device\n");
        return;
    }

    const auto received = [&s] { return s.viewer.audioStats().jitter.framesReceived; };
    Check(WaitFor([&received] { return received() > 0; }, kConnectTimeoutMs),
        "audio is flowing before the transfer starts");

    Continuity audio(received);
    audio.Begin();
    Check(s.viewer.SendFiles({s.file}), "the batch is offered");

    bool finished = false;
    for (uint32_t waited = 0; waited < kTransferTimeoutMs * 1000 / kSampleUs; ++waited) {
        SleepUs(kSampleUs);
        audio.Sample();
        if (!s.viewer.uploading()) {
            finished = true;
            break;
        }
    }

    std::printf("        %llu audio frames during the upload, worst gap %llu ms\n",
        static_cast<unsigned long long>(audio.moved()),
        static_cast<unsigned long long>(audio.worstGapMs()));

    Check(finished, "the transfer settles inside the deadline");
    Check(audio.moved() > 0, "audio frames kept arriving while the file was crossing");
    Check(audio.worstGapMs() < kWorstGapMs,
        "and never went silent for anything like the seconds a reliable stream shared with "
        "the control channel could cost it");
    Check(s.LandedWhole(), "and the file still landed whole");
}

}

void RunTransferUnderLoadTests() {
    TestABigTransferNeverStallsTheStream();
    TestInputStaysLiveDuringABigTransfer();
    TestAudioKeepsFlowingDuringABigTransfer();
}
