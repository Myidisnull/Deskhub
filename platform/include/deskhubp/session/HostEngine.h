#pragma once
#include "deskhub/control/QualityLadder.h"
#include "deskhub/control/StreamSize.h"
#include "deskhub/media/AgentTypes.h"
#include "deskhub/protocol/Wire.h"
#include "deskhub/session/Beacon.h"
#include "deskhub/session/SourcePipelineState.h"
#include "deskhubp/input/LocalInput.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/session/HostAgent.h"
#include "deskhubp/session/HostNetLoop.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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

    bool running() const {
        return running_.load(std::memory_order_acquire);
    }

    std::vector<deskhub::media::AgentSourceStatus> Status();
    std::string LastError();

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

    deskhub::media::AgentOptions opt_;
    HostEnginePolicy policy_;

    UdpSocket sock_;
    std::thread recvThread_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> running_{false};

    std::vector<std::unique_ptr<HostSource>> pipes_;
    std::vector<HostSource*> live_;
    uint8_t nextSourceId_ = 0;

    std::mutex statusMutex_;
    std::vector<deskhub::media::AgentSourceStatus> statusRows_;

    std::mutex errMutex_;
    std::string lastError_;

    LocalInputMonitor localInputMon_;
    deskhub::Beacon beacon_;

    uint32_t startBitrateBps_ = 0;
    NetAddr replyAddr_{};
};

}
