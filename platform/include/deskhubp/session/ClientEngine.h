#pragma once
#include "deskhub/control/ClockOffset.h"
#include "deskhub/diag/ClientDiag.h"
#include "deskhub/input/ClientInputQueue.h"
#include "deskhub/media/VideoContract.h"
#include "deskhub/protocol/Wire.h"
#include "deskhub/session/ClientPump.h"
#include "deskhub/transport/Reassembler.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/diag/LogFile.h"
#include "deskhubp/net/ClientNetLoop.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/system/Clock.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cinttypes>
#include <cstdint>
#include <deque>
#include <mutex>
#include <functional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace deskhubp {

enum class ClientPhase : int32_t { Idle = 0,
    Connecting = 1,
    Streaming = 2,
    Ended = 3 };

struct ClientEngineConfig {
    NetAddr server{};
    uint8_t sourceId = 0;
    uint32_t screenW = 0;
    uint32_t screenH = 0;
    uint8_t desiredFps = 60;
    bool sendNacks = true;
    bool logLossRuns = true;
    bool alwaysFocused = false;
    const char* statusSeparator = "  ";
    std::string passcode;
    std::string displayName;

    std::function<void(uint32_t width, uint32_t height, uint8_t fps)> onParams;
    std::function<void(const char* status)> onStatus;
    std::function<void(const char* reason)> onEnded;
    std::function<void(const char* reason)> onFinished;
    std::function<void()> onDecodeThreadStart;
    std::function<void()> onDecodeThreadExit;
};

template <class Decoder, class Surface>
    requires deskhub::media::EngineDecoder<Decoder, Surface>
class ClientEngine {
public:
    static constexpr deskhub::diag::ClientDiagCaps kDecoderDiagCaps{
        deskhub::media::PresentTimingDecoder<Decoder>,
        deskhub::media::CongestionAwareDecoder<Decoder>};

    explicit ClientEngine(deskhub::diag::ClientDiagCaps caps = kDecoderDiagCaps) : diag_(caps) {}

    ~ClientEngine() {
        Stop();
    }

    ClientEngine(const ClientEngine&) = delete;
    ClientEngine& operator=(const ClientEngine&) = delete;

    bool Start(const ClientEngineConfig& cfg) {
        cfg_ = cfg;
        if (!sock_.Open(0)) {
            LOGE("[Client] Failed to open socket.");
            return false;
        }
        sock_.SetRecvTimeout(10);

        quit_.store(false);
        finished_.store(false);
        phase_.store(ClientPhase::Connecting, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lk(textMutex_);
            statusLine_.clear();
            endReason_.clear();
        }

        {
            std::lock_guard<std::mutex> lk(surfaceMutex_);
            decodeExited_ = false;
            decodeRunning_ = true;
            surfaceAckGen_ = surfaceGen_;
        }
        decodeThread_ = std::thread([this] { DecodeThread(); });
        netThread_ = std::thread([this] { NetThread(); });
        LOGI("[Client] Connecting to %s (source %u) ...", cfg_.server.ToString().c_str(),
            cfg_.sourceId);
        return true;
    }

    bool Start(const NetAddr& server, uint8_t sourceId, uint32_t screenW, uint32_t screenH) {
        ClientEngineConfig cfg;
        cfg.server = server;
        cfg.sourceId = sourceId;
        cfg.screenW = screenW;
        cfg.screenH = screenH;
        return Start(cfg);
    }

    void Stop() {
        quit_.store(true);
        decCv_.notify_all();
        surfaceCv_.notify_all();
        if (netThread_.joinable()) netThread_.join();
        if (decodeThread_.joinable()) decodeThread_.join();
        sock_.Close();
    }

    void SetSurface(Surface surface) {
        std::unique_lock<std::mutex> lk(surfaceMutex_);
        surface_ = surface;
        ++surfaceGen_;
        surfaceCv_.notify_all();
        decCv_.notify_all();
        if (!decodeRunning_) return;
        surfaceAckCv_.wait(
            lk, [this] { return surfaceAckGen_ == surfaceGen_ || decodeExited_; });
    }

    ClientPhase phase() const {
        return phase_.load(std::memory_order_acquire);
    }

    uint32_t videoWidth() const {
        return negW_.load();
    }
    uint32_t videoHeight() const {
        return negH_.load();
    }

    std::string StatusLine() {
        std::lock_guard<std::mutex> lk(textMutex_);
        return statusLine_;
    }

    std::string EndReason() {
        std::lock_guard<std::mutex> lk(textMutex_);
        return endReason_;
    }

    void QueueKey(int32_t vk, int32_t scan, bool down) {
        input_.Key(vk, scan, down, NowUs());
    }
    void QueueKeyTap(int32_t vk, int32_t scan) {
        input_.KeyTap(vk, scan, NowUs());
    }
    void QueueKeyChord(int32_t modVk, int32_t modScan, int32_t vk, int32_t scan) {
        input_.KeyChord(modVk, modScan, vk, scan, NowUs());
    }
    void QueueCharTap(uint32_t codepoint) {
        input_.CharTap(codepoint, NowUs());
    }
    void QueueMouseMoveAbs(int32_t nx, int32_t ny) {
        input_.MouseMoveAbsolute(nx, ny, NowUs());
    }
    void QueueMouseMoveRel(int32_t dx, int32_t dy) {
        input_.MouseMoveRelative(dx, dy, NowUs());
    }
    void QueueMouseButton(int32_t button, bool down) {
        input_.MouseButtonEvent(button, down, NowUs());
    }
    void QueueMouseWheel(int32_t delta) {
        input_.MouseWheel(delta, NowUs());
    }
    void ReleaseAllInput() {
        input_.ReleaseAll(NowUs());
    }

    void ReportPresented(uint64_t ptsUs, uint64_t shownUs) {
        if (!ptsUs) return;
        clockOffset_.AddSample(ptsUs, shownUs);
        lastE2eUs_.store(clockOffset_.LatencyUs(diag_.minRttUs.value() / 2));
    }

    deskhub::diag::ClientDiag& diag() {
        return diag_;
    }

private:
    static constexpr size_t kMaxQueuedFrames = 3;
    static constexpr uint64_t kSlowDecodeMs = 20;

    bool TakeSurface(Surface& out) {
        std::lock_guard<std::mutex> lk(surfaceMutex_);
        out = surface_;
        return SurfaceIsUsable(out);
    }

    static bool SurfaceIsUsable(const Surface& s) {
        if constexpr (std::is_pointer_v<Surface>)
            return s != nullptr;
        else if constexpr (requires { { s.valid() } -> std::convertible_to<bool>; })
            return s.valid();
        else
            return true;
    }

    bool AckSurfaceSwap(Decoder& decoder) {
        std::unique_lock<std::mutex> lk(surfaceMutex_);
        if (surfaceAckGen_ == surfaceGen_) return false;

        const uint64_t generation = surfaceGen_;
        lk.unlock();
        const bool hadDecoder = decoder.IsOpen();
        decoder.Shutdown();
        lk.lock();

        surfaceAckGen_ = generation;
        surfaceAckCv_.notify_all();
        rebuildDecoder_.store(false);
        if (hadDecoder) decodeFailed_.store(true, std::memory_order_release);
        return true;
    }

    bool NextFrame(deskhub::Reassembler::Frame& out) {
        std::unique_lock<std::mutex> lk(decMutex_);
        decCv_.wait_for(lk, std::chrono::milliseconds(20),
            [this] { return quit_.load() || !decQueue_.empty(); });
        if (decQueue_.empty()) return false;
        out = std::move(decQueue_.front());
        decQueue_.pop_front();
        return true;
    }

    bool EnsureDecoder(Decoder& decoder) {
        if (rebuildDecoder_.exchange(false) && decoder.IsOpen()) decoder.Shutdown();
        if (decoder.IsOpen()) return true;

        const uint32_t w = negW_.load(), h = negH_.load();
        if (!w || !h) return false;

        Surface surface{};
        if (!TakeSurface(surface)) return false;

        if (!decoder.Init(surface, int(w), int(h))) {
            decodeFailed_.store(true, std::memory_order_release);
            return false;
        }
        return true;
    }

    void HarvestDecoder(Decoder& decoder) {
        if constexpr (deskhub::media::RenderCountingDecoder<Decoder>) {
            if (const uint32_t n = decoder.TakeRenderedCount())
                stRendered_.fetch_add(n, std::memory_order_relaxed);
        }
        if constexpr (deskhub::media::CongestionAwareDecoder<Decoder>) {
            if (const uint32_t n = decoder.TakeCongestionDrops()) {
                diag_.dispDrop.Add(n);
                displayCongested_.store(true, std::memory_order_release);
            }
        }
        if constexpr (deskhub::media::PresentTimingDecoder<Decoder>) {
            if (const uint32_t ms = decoder.TakePresentDelayMs()) diag_.presentMs.Add(ms);
        }
        if constexpr (deskhub::media::RenderCountingDecoder<Decoder>) {
            if (const uint64_t pts = decoder.lastRenderedPtsUs()) {
                uint64_t shownUs = NowUs();
                if constexpr (deskhub::media::PresentTimingDecoder<Decoder>) {
                    if (const uint64_t at = decoder.lastRenderedAtUs()) shownUs = at;
                }
                ReportPresented(pts, shownUs);
            }
        }
    }

    void DecodeThread() {
        if (cfg_.onDecodeThreadStart) cfg_.onDecodeThreadStart();

        {
            Decoder decoder;
            clockOffset_.Reset();

            for (;;) {
                AckSurfaceSwap(decoder);
                if (quit_.load()) break;

                deskhub::Reassembler::Frame frame;
                if (!NextFrame(frame)) continue;
                if (!EnsureDecoder(decoder)) continue;

                const uint64_t t0 = NowUs();
                const bool ok = decoder.Decode(frame.nal.data(), frame.nal.size(),
                    frame.timestampUs);
                const uint64_t decMs = (NowUs() - t0) / 1000;
                diag_.decMs.Add(uint32_t(decMs));
                if (decMs > kSlowDecodeMs)
                    LOGW("[Client] decode took %" PRIu64 " ms for one frame", decMs);

                if (!ok) {
                    decodeFailed_.store(true, std::memory_order_release);
                    decoder.Shutdown();
                    continue;
                }

                HarvestDecoder(decoder);
            }

            decoder.Shutdown();
        }

        {
            std::lock_guard<std::mutex> lk(surfaceMutex_);
            decodeExited_ = true;
            decodeRunning_ = false;
            surfaceAckGen_ = surfaceGen_;
        }
        surfaceAckCv_.notify_all();

        if (cfg_.onDecodeThreadExit) cfg_.onDecodeThreadExit();
    }

    deskhub::ClientPumpCallbacks MakePumpCallbacks() {
        deskhub::ClientPumpCallbacks cb;
        cb.send = [this](std::span<const uint8_t> d) {
            sock_.SendTo(cfg_.server, d.data(), d.size());
        };
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
            if (cfg_.onParams) cfg_.onParams(np.width, np.height, np.fps);
        };
        cb.onEnded = [this](const char* reason) {
            {
                std::lock_guard<std::mutex> lk(textMutex_);
                endReason_ = reason;
            }
            if (cfg_.onEnded) cfg_.onEnded(reason ? reason : "disconnected");
            quit_.store(true);
        };
        cb.takeRenderedCount = [this] {
            return stRendered_.exchange(0, std::memory_order_relaxed);
        };
        cb.latencyUs = [this] { return lastE2eUs_.load(); };
        cb.onStatus = [this](const char* compact) {
            {
                std::lock_guard<std::mutex> lk(textMutex_);
                statusLine_ = compact;
            }
            if (cfg_.onStatus) cfg_.onStatus(compact);
        };
        cb.localTime = [] { return LocalTimeHms(); };
        cb.log = [](bool warn, const char* line) {
            if (warn) {
                LOGW("%s", line);
                return;
            }
            LOGI("%s", line);
        };
        return cb;
    }

    void NetThread() {
        deskhub::ClientPump pump(MakePumpCallbacks(), diag_);

        deskhub::ClientPumpConfig pcfg;
        pcfg.clientId = MakeClientId(cfg_.sourceId);
        pcfg.maxWidth = uint16_t(cfg_.screenW);
        pcfg.maxHeight = uint16_t(cfg_.screenH);
        pcfg.sourceId = cfg_.sourceId;
        pcfg.desiredFps = cfg_.desiredFps;
        pcfg.sendNacks = cfg_.sendNacks;
        pcfg.logLossRuns = cfg_.logLossRuns;
        pcfg.statusSeparator = cfg_.statusSeparator;
        pcfg.passcode = cfg_.passcode;
        pcfg.displayName = cfg_.displayName;
        pump.Start(pcfg, NowUs());

        std::vector<deskhub::InputEvent> batch;

        ClientNetLoopHooks hooks;
        hooks.stopped = [this] { return quit_.load(); };
        hooks.afterFrames = [this](deskhub::ClientPump& p, uint64_t now) {
            if (decodeFailed_.exchange(false, std::memory_order_acq_rel))
                p.RequestKeyframe("dec_fail", now);
            if (displayCongested_.exchange(false, std::memory_order_acq_rel))
                p.RequestKeyframe("display_congested", now);
            if (queueOverflow_.exchange(false, std::memory_order_acq_rel))
                p.RequestKeyframe("q_overflow", now);
        };
        hooks.beforeTick = [this, &batch](deskhub::ClientPump& p, uint64_t now) {
            input_.Drain(now, batch);
            for (const auto& e : batch) p.QueueInput(e);
            if (cfg_.alwaysFocused || input_.wantsFocus()) p.SetFocused(true);
        };
        hooks.onPhase = [this](bool streaming) {
            phase_.store(streaming ? ClientPhase::Streaming : ClientPhase::Connecting,
                std::memory_order_release);
        };
        hooks.onSocketError = [this] {
            LOGE("[Client] Socket error.");
            std::lock_guard<std::mutex> lk(textMutex_);
            endReason_ = "socket error";
        };

        RunClientNetLoop(sock_, pump, hooks);

        quit_.store(true);
        decCv_.notify_all();
        {
            std::lock_guard<std::mutex> lk(textMutex_);
            if (endReason_.empty()) endReason_ = "stopped";
        }
        phase_.store(ClientPhase::Ended, std::memory_order_release);
        finished_.store(true, std::memory_order_release);
        if (cfg_.onFinished) {
            std::string reason;
            {
                std::lock_guard<std::mutex> lk(textMutex_);
                reason = endReason_;
            }
            cfg_.onFinished(reason.c_str());
        }
        LOGI("[Client] Session ended.");
    }

    ClientEngineConfig cfg_{};
    UdpSocket sock_;

    std::thread netThread_;
    std::thread decodeThread_;

    std::atomic<bool> quit_{false};
    std::atomic<bool> finished_{false};
    std::atomic<ClientPhase> phase_{ClientPhase::Idle};

    std::mutex textMutex_;
    std::string statusLine_;
    std::string endReason_;

    std::atomic<uint32_t> negW_{0}, negH_{0};
    std::atomic<bool> rebuildDecoder_{false};

    std::mutex surfaceMutex_;
    std::condition_variable surfaceCv_;
    std::condition_variable surfaceAckCv_;
    Surface surface_{};
    uint64_t surfaceGen_ = 0;
    uint64_t surfaceAckGen_ = 0;
    bool decodeExited_ = false;
    bool decodeRunning_ = false;

    std::mutex decMutex_;
    std::condition_variable decCv_;
    std::deque<deskhub::Reassembler::Frame> decQueue_;

    std::atomic<bool> decodeFailed_{false};
    std::atomic<bool> displayCongested_{false};
    std::atomic<bool> queueOverflow_{false};
    std::atomic<uint32_t> stRendered_{0};

    deskhub::ClientInputQueue input_;
    deskhub::diag::ClientDiag diag_;

    std::atomic<int64_t> lastE2eUs_{-1};
    deskhub::ClockOffset clockOffset_;
};

}
