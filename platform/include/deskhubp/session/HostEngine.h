#pragma once
#include "deskhub/control/QualityLadder.h"
#include "deskhub/control/StreamSize.h"
#include "deskhub/media/AgentTypes.h"
#include "deskhub/media/VideoTypes.h"
#include "deskhub/protocol/Wire.h"
#include "deskhub/session/Beacon.h"
#include "deskhub/session/SourcePipelineState.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/input/LocalInput.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/session/HostAgent.h"
#include "deskhubp/session/HostNetLoop.h"
#include "deskhubp/system/Clock.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace deskhubp {

using HostSource = deskhub::SourcePipelineState;

struct HostSourcePolicy {
    std::function<std::unique_ptr<HostSource>(const deskhub::media::ShareSource&,
        uint8_t sourceId)>
        create;
    std::function<void(HostSource&)> startCapture;
    std::function<void(HostSource&)> stopCapture;
    std::function<void(HostSource&)> attachInput;
    std::function<void(HostSource&)> releaseInput;
    std::function<void(HostSource&, const deskhub::InputEvent&)> applyInput;
    std::function<bool(HostSource&, uint32_t bitrateBps)> setEncoderBitrate;
    std::function<deskhub::StreamSize(HostSource&)> retarget;
    std::function<deskhub::StreamSize(HostSource&, const deskhub::QualityStep& prev,
        const deskhub::QualityStep& next)>
        applyQualityStep;
    std::function<void(HostSource&, uint64_t nowUs)> flush;
    std::function<uint64_t(const HostSource&)> inputSkipped;
    std::function<uint32_t(HostSource&)> takeIdleFrames;
};

struct HostEnginePolicy {
    std::function<std::string()> preflight;
    std::function<std::string()> afterSocket;
    std::function<void()> onSharing;
    std::function<std::string(const UdpSocket&)> portError;

    std::string noSourceError = "No source selected.";
    std::string noUsableSourceError = "No usable source \xE2\x80\x94 stopping.";

    HostSourcePolicy source;
    SourceStatusHooks status;
};

class HostEngine {
public:
    static constexpr uint32_t kMinBitrateBps = 1'000'000;

    HostEngine() = default;
    ~HostEngine();
    HostEngine(const HostEngine&) = delete;
    HostEngine& operator=(const HostEngine&) = delete;

    bool Start(const std::vector<deskhub::media::ShareSource>& sources,
        const deskhub::media::AgentOptions& opt, HostEnginePolicy policy);
    void Stop();

    void RequestStopSource(uint8_t sourceId);
    void RequestKickViewer(uint8_t sourceId, uint64_t addrPacked);

    bool running() const {
        return running_.load(std::memory_order_acquire);
    }

    std::vector<deskhub::media::AgentSourceStatus> Status();
    std::string LastError();

    deskhub::media::PacketHandler MakePacketSink(HostSource& st) {
        return [this, p = &st](const uint8_t* data, size_t size, uint64_t tsUs, bool keyframe) {
            SendEncodedFrame(*p, sock_, std::span<const uint8_t>(data, size), tsUs, keyframe);
        };
    }

    const deskhub::media::AgentOptions& options() const {
        return opt_;
    }
    uint32_t startBitrateBps() const {
        return startBitrateBps_;
    }
    LocalInputMonitor& localInput() {
        return localInputMon_;
    }
    UdpSocket& socket() {
        return sock_;
    }

private:
    bool Fail(std::string message);
    void AttachSession(HostSource& st);
    void ShutdownSource(HostSource& st);
    void PublishStatus();
    std::vector<HostSource*> AllSources();
    void RecvLoop();
    void DrainControlRequests();
    HostSource* FindLiveSource(uint8_t sourceId);

    deskhub::media::AgentOptions opt_;
    HostEnginePolicy policy_;

    UdpSocket sock_;
    std::thread recvThread_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> running_{false};

    deskhub::ViewerBudget viewerBudget_;
    std::vector<std::unique_ptr<HostSource>> pipes_;
    std::vector<HostSource*> live_;
    uint8_t nextSourceId_ = 0;

    std::mutex statusMutex_;
    std::vector<deskhub::media::AgentSourceStatus> statusRows_;

    std::mutex controlMutex_;
    std::vector<uint8_t> pendingSourceStops_;
    std::vector<std::pair<uint8_t, uint64_t>> pendingViewerKicks_;

    std::mutex errMutex_;
    std::string lastError_;

    LocalInputMonitor localInputMon_;
    deskhub::Beacon beacon_;

    uint32_t startBitrateBps_ = 0;
};

template <class Capture, class Injector, class Encoder>
struct HostSourceBase : HostSource {
    HostSourceBase(uint32_t startBps, uint32_t minBps, deskhub::diag::AgentDiagCaps caps)
        : HostSource(startBps, minBps, caps) {}

    Capture capture;
    Injector injector;

    std::mutex encMutex;
    std::unique_ptr<Encoder> encoder;

    bool hasCachedFrame() const {
        return haveCached_.load(std::memory_order_acquire);
    }

    void SetCachedFrame(bool present) {
        haveCached_.store(present, std::memory_order_release);
    }

private:
    std::atomic<bool> haveCached_{false};
};

template <class Pipeline>
std::unique_ptr<Pipeline> MakeHostSource(HostEngine& engine,
    const deskhub::media::ShareSource& s, uint8_t sourceId) {
    auto p = std::make_unique<Pipeline>(engine.startBitrateBps(), HostEngine::kMinBitrateBps);
    p->sourceId = sourceId;
    p->name = s.name;
    return p;
}

template <class Pipeline>
HostSourcePolicy MakeDefaultSourcePolicy() {
    HostSourcePolicy sp;
    sp.releaseInput = [](HostSource& st) { static_cast<Pipeline&>(st).injector.ReleaseAll(); };
    sp.applyInput = [](HostSource& st, const deskhub::InputEvent& e) {
        static_cast<Pipeline&>(st).injector.Apply(e);
    };
    sp.setEncoderBitrate = [](HostSource& st, uint32_t bitrateBps) {
        Pipeline& p = static_cast<Pipeline&>(st);
        std::lock_guard<std::mutex> lk(p.encMutex);
        return p.encoder && p.encoder->SetBitrate(bitrateBps);
    };
    sp.inputSkipped = [](const HostSource& st) {
        return static_cast<const Pipeline&>(st).injector.skipped();
    };
    return sp;
}

template <class Pipeline>
SourceStatusHooks MakeDefaultStatusHooks() {
    SourceStatusHooks hooks;
    hooks.closed = [](const HostSource& st) {
        return static_cast<const Pipeline&>(st).capture.Closed();
    };
    return hooks;
}

template <class Fn>
bool DiagEncode(HostSource& st, bool idr, Fn&& encode) {
    const uint64_t t0 = NowUs();
    const bool ok = std::forward<Fn>(encode)();
    const uint32_t ms = uint32_t((NowUs() - t0) / 1000);
    st.diag.encMs.Add(ms);
    if (!ok) LOGW("[DIAG][%s] evt=enc_fail idr=%d ms=%u", st.name.c_str(), idr ? 1 : 0, ms);
    return ok;
}

}
