#include "ClientLoop.h"

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <memory>
#include <utility>

#include "deskhubp/Log.h"
#include "decode/AvDecoder.h"
#include "deskhubp/Clock.h"
#include "deskhubp/LogFile.h"
#include "render/VideoSink.h"

#include "deskhub/protocol/Wire.h"
#include "deskhub/session/ClientPump.h"

ClientLoop::~ClientLoop() {
    Stop();
}

std::string ClientLoop::StatusLine() {
    std::lock_guard<std::mutex> lk(textMutex_);
    return statusLine_;
}

std::string ClientLoop::EndReason() {
    std::lock_guard<std::mutex> lk(textMutex_);
    return endReason_;
}

void ClientLoop::QueueKey(int32_t vk, int32_t scan, bool down) {
    input_.Key(vk, scan, down, NowUs());
}

void ClientLoop::ReleaseAllInput() {
    input_.ReleaseAll(NowUs());
}

void ClientLoop::QueueMouseMoveAbs(int32_t nx, int32_t ny) {
    input_.MouseMoveAbsolute(nx, ny, NowUs());
}

void ClientLoop::QueueMouseMoveRel(int32_t dx, int32_t dy) {
    input_.MouseMoveRelative(dx, dy, NowUs());
}

void ClientLoop::QueueMouseButton(int32_t button, bool down) {
    input_.MouseButtonEvent(button, down, NowUs());
}

void ClientLoop::QueueMouseWheel(int32_t delta) {
    input_.MouseWheel(delta, NowUs());
}

bool ClientLoop::Start(const NetAddr& server, uint8_t sourceId, VideoSink* sink,
    uint32_t screenW, uint32_t screenH) {
    server_ = server;
    sourceId_ = sourceId;
    sink_ = sink;
    screenW_ = screenW;
    screenH_ = screenH;
    if (!sock_.Open(0)) {
        LOGE("[Client] Failed to open socket.");
        return false;
    }
    sock_.SetRecvTimeout(10);

    quit_.store(false);
    phase_.store(Phase::Connecting, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lk(textMutex_);
        statusLine_.clear();
        endReason_.clear();
    }
    decodeThread_ = std::thread([this] { DecodeThread(); });
    netThread_ = std::thread([this] { NetThread(); });
    LOGI("[Client] Connecting to %s (source %u) ...", server_.ToString().c_str(), sourceId_);
    return true;
}

void ClientLoop::Stop() {
    quit_.store(true);
    decCv_.notify_all();
    if (netThread_.joinable()) netThread_.join();
    if (decodeThread_.joinable()) decodeThread_.join();
    sock_.Close();
}

void ClientLoop::DecodeThread() {
    AvDecoder decoder;
    clockOffset_.Reset();

    while (!quit_.load()) {
        deskhub::Reassembler::Frame f;
        {
            std::unique_lock<std::mutex> lk(decMutex_);
            decCv_.wait_for(lk, std::chrono::milliseconds(20),
                [this] { return quit_.load() || !decQueue_.empty(); });
            if (decQueue_.empty()) continue;
            f = std::move(decQueue_.front());
            decQueue_.pop_front();
        }

        if (rebuildDecoder_.exchange(false) && decoder.IsOpen()) decoder.Shutdown();

        if (!decoder.IsOpen()) {
            const uint32_t w = negW_.load(), h = negH_.load();
            if (!w || !h) continue;
            if (!decoder.Init(sink_, int(w), int(h))) {
                decodeFailed_.store(true, std::memory_order_release);
                continue;
            }
        }

        const uint64_t t0 = NowUs();
        const bool ok = decoder.Decode(f.nal.data(), f.nal.size(), f.timestampUs);
        const uint64_t decMs = (NowUs() - t0) / 1000;
        diag_.decMs.Add(uint32_t(decMs));
        if (decMs > 20) LOGW("[Client] decode took %" PRIu64 " ms for one frame", decMs);

        if (!ok) {
            decodeFailed_.store(true, std::memory_order_release);
            decoder.Shutdown();
            continue;
        }

        if (const uint64_t pts = sink_ ? sink_->lastRenderedPtsUs() : 0) {
            clockOffset_.AddSample(pts, NowUs());
            lastE2eUs_.store(clockOffset_.LatencyUs(diag_.minRttUs.value() / 2),
                std::memory_order_relaxed);
        }
    }

    decoder.Shutdown();
}

void ClientLoop::NetThread() {
    deskhub::ClientPumpCallbacks cb;
    cb.send = [this](std::span<const uint8_t> d) { sock_.SendTo(server_, d.data(), d.size()); };
    cb.onFrame = [this](deskhub::Reassembler::Frame&& f) {
        {
            std::lock_guard<std::mutex> lk(decMutex_);
            if (decQueue_.size() >= kMaxQueuedFrames) {
                decQueue_.pop_front();
                queueOverflow_.store(true, std::memory_order_release);
                diag_.dqDrop.Add();
            }
            decQueue_.push_back(std::move(f));
        }
        decCv_.notify_one();
    };
    cb.onParams = [this](const deskhub::NegotiatedParams& np, bool reconfigured) {
        negW_.store(np.width);
        negH_.store(np.height);
        if (reconfigured) rebuildDecoder_.store(true);
    };
    cb.onEnded = [this](const char* reason) {
        {
            std::lock_guard<std::mutex> lk(textMutex_);
            endReason_ = reason;
        }
        quit_.store(true);
    };
    cb.takeRenderedCount = [this] { return sink_ ? sink_->TakeRenderedCount() : 0u; };
    cb.latencyUs = [this] { return lastE2eUs_.load(std::memory_order_relaxed); };
    cb.onStatus = [this](const char* compact) {
        std::lock_guard<std::mutex> lk(textMutex_);
        statusLine_ = compact;
    };
    cb.localTime = [] { return deskhubp::LocalTimeHms(); };
    cb.log = [](bool warn, const char* line) {
        if (warn)
            LOGW("%s", line);
        else
            LOGI("%s", line);
    };

    deskhub::ClientPump pump(std::move(cb), diag_);

    deskhub::ClientPumpConfig pcfg;
    pcfg.clientId = uint32_t(NowUs());
    pcfg.maxWidth = uint16_t(screenW_);
    pcfg.maxHeight = uint16_t(screenH_);
    pcfg.sourceId = sourceId_;
    pcfg.desiredFps = 60;
    pcfg.sendNacks = true;
    pcfg.logLossRuns = true;
    pump.Start(pcfg, NowUs());

    uint8_t buf[deskhub::kMaxDatagram];
    std::vector<deskhub::InputEvent> inputBatch;

    while (!quit_.load()) {
        NetAddr from;
        const int n = sock_.RecvFrom(buf, sizeof(buf), from);
        const uint64_t now = NowUs();
        if (n < 0) {
            LOGE("[Client] Socket error.");
            std::lock_guard<std::mutex> lk(textMutex_);
            endReason_ = "socket error";
            break;
        }
        if (n > 0) pump.OnDatagram(std::span<const uint8_t>(buf, size_t(n)), now);

        pump.PollFrames(now);
        if (decodeFailed_.exchange(false, std::memory_order_acq_rel))
            pump.RequestKeyframe("dec_fail", now);
        if (queueOverflow_.exchange(false, std::memory_order_acq_rel))
            pump.RequestKeyframe("q_overflow", now);

        pump.PlanNacks(now);

        input_.Drain(now, inputBatch);
        for (const auto& e : inputBatch) pump.QueueInput(e);
        if (input_.wantsFocus()) pump.SetFocused(true);

        if (!pump.Tick(now)) break;

        phase_.store(pump.streaming() ? Phase::Streaming : Phase::Connecting,
            std::memory_order_release);

        pump.CountLoopBusy(now, NowUs());
    }

    pump.SendBye();
    quit_.store(true);
    decCv_.notify_all();
    {
        std::lock_guard<std::mutex> lk(textMutex_);
        if (endReason_.empty()) endReason_ = "stopped";
    }
    phase_.store(Phase::Ended, std::memory_order_release);
    LOGI("[Client] Session ended.");
}
