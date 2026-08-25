#pragma once
#include "deskhub/control/ClockOffset.h"
#include "deskhub/diag/ClientDiag.h"
#include "deskhub/input/ClientInputQueue.h"
#include "deskhub/media/VideoContract.h"
#include "deskhub/protocol/Wire.h"
#include "deskhub/session/ClientPump.h"
#include "deskhub/session/ClientReconnect.h"
#include "deskhub/session/LinkPulse.h"
#include "deskhub/transport/Reassembler.h"
#include "deskhub/ui/Strings.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/diag/LogFile.h"
#include "deskhubp/diag/StallLog.h"
#include "deskhubp/audio/AudioPlayer.h"
#include "deskhubp/client/FileUpload.h"
#include "deskhubp/net/ClientNetLoop.h"
#include "deskhubp/net/FileUdp.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/system/KeepAwake.h"
#include "deskhubp/system/Random.h"
#include "deskhubp/system/UiSettingsStore.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cinttypes>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <functional>
#include <optional>
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
    bool wantsAudio = false;
    const char* statusSeparator = "  ";
    std::string passcode;
    std::string displayName;
    std::string sessionKeyHex;

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

    explicit ClientEngine(deskhub::diag::ClientDiagCaps caps = kDecoderDiagCaps)
        : diag_(caps) {}

    ~ClientEngine() {
        Stop();
    }

    ClientEngine(const ClientEngine&) = delete;
    ClientEngine& operator=(const ClientEngine&) = delete;

    bool Start(const ClientEngineConfig& cfg) {
        if (netThread_.joinable() || decodeThread_.joinable()) {
            if (!finished_.load(std::memory_order_acquire)) return false;
            if (netThread_.joinable()) netThread_.join();
            if (decodeThread_.joinable()) decodeThread_.join();
        }
        cfg_ = cfg;
        if (!OpenSocket()) {
            LOGE("[Client] Failed to open socket.");
            return false;
        }

        {
            std::lock_guard<std::mutex> lk(linkMutex_);
            linkView_ = {};
        }

        userStop_.store(false);
        sessionDone_.store(false);
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
        SyncKeepAwakeHeld(true);
        if (cfg_.wantsAudio && !player_.Start())
            LOGW("[Client] Watching without sound: no audio device could be opened.");
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
        const uint64_t tAll = NowUs();
        LOGI("[DIAG][client] evt=stop_begin");
        userStop_.store(true);
        sessionDone_.store(true);
        quit_.store(true);
        player_.Stop();
        decCv_.notify_all();
        surfaceCv_.notify_all();
        {
            StopAnrWatch watch("client", "net_join");
            const uint64_t t0 = NowUs();
            if (netThread_.joinable()) netThread_.join();
            LogStopPhase("client", "net_join", t0);
        }
        {
            StopAnrWatch watch("client", "decode_join");
            const uint64_t t0 = NowUs();
            if (decodeThread_.joinable()) decodeThread_.join();
            LogStopPhase("client", "decode_join", t0);
        }
        sock_.Close();
        SyncKeepAwakeHeld(false);
        LogStopPhase("client", "stop_total", tAll);
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

    deskhub::LinkPulseView LinkHealth() const {
        std::lock_guard<std::mutex> lk(linkMutex_);
        return linkView_;
    }

    bool BeginFileSend(const std::vector<std::filesystem::path>& paths) {
        FileUpload* upload = fileUpload_.load(std::memory_order_acquire);
        if (!upload) {
            std::lock_guard<std::mutex> lk(fileMutex_);
            fileError_ = "not connected";
            return false;
        }
        const bool ok = upload->Begin(paths);
        std::lock_guard<std::mutex> lk(fileMutex_);
        if (!ok)
            fileError_ = upload->LastError();
        else
            fileError_.clear();
        return ok;
    }

    void CancelFileSend() {
        if (FileUpload* upload = fileUpload_.load(std::memory_order_acquire)) upload->Cancel();
    }

    bool FileSendBusy() const {
        if (const FileUpload* upload = fileUpload_.load(std::memory_order_acquire))
            return upload->Busy();
        return false;
    }

    deskhub::FileSenderState FileSendState() const {
        if (const FileUpload* upload = fileUpload_.load(std::memory_order_acquire))
            return upload->State();
        return deskhub::FileSenderState::Idle;
    }

    deskhub::TransferProgress FileSendProgress() const {
        std::lock_guard<std::mutex> lk(fileMutex_);
        return fileProgress_;
    }

    std::string FileSendError() const {
        std::lock_guard<std::mutex> lk(fileMutex_);
        return fileError_;
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

    void OfferLocalClipboard(std::string text) {
        std::lock_guard<std::mutex> lk(clipMutex_);
        pendingLocalClip_ = std::move(text);
    }

    std::optional<std::string> TakeRemoteClipboard() {
        std::lock_guard<std::mutex> lk(clipMutex_);
        if (remoteClips_.empty()) return std::nullopt;
        std::string text = std::move(remoteClips_.front());
        remoteClips_.pop_front();
        return text;
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
        cb.onAudioPacket = [this](const deskhub::AudioPacketView& v) { player_.Push(v); };
        cb.onClipboardText = [this](std::string_view text) {
            std::lock_guard<std::mutex> lk(clipMutex_);
            remoteClips_.emplace_back(text);
            while (remoteClips_.size() > 4) remoteClips_.pop_front();
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
                endReason_ = reason ? reason : "disconnected";
            }
            sessionDone_.store(true, std::memory_order_release);
        };
        cb.randomBytes = [](std::span<uint8_t> out) {
            return RandomBytes(out.data(), out.size());
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
        cb.onLinkPulse = [this](const deskhub::LinkPulseView& view) {
            std::lock_guard<std::mutex> lk(linkMutex_);
            linkView_ = view;
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

    bool OpenSocket() {
        sock_.Close();
        if (!sock_.Open(0)) return false;
        sock_.SetRecvTimeout(10);
        return true;
    }

    void ClearDecodeQueue() {
        std::lock_guard<std::mutex> lk(decMutex_);
        decQueue_.clear();
    }

    void PublishStatus(const char* status) {
        {
            std::lock_guard<std::mutex> lk(textMutex_);
            statusLine_ = status ? status : "";
        }
        if (cfg_.onStatus) cfg_.onStatus(status);
    }

    bool WaitReconnectBackoff(uint64_t delayUs) {
        constexpr uint64_t kSliceUs = 50'000;
        uint64_t left = delayUs;
        while (left > 0) {
            if (userStop_.load(std::memory_order_acquire)) return false;
            const uint64_t slice = left < kSliceUs ? left : kSliceUs;
            SleepUs(slice);
            left -= slice;
        }
        return !userStop_.load(std::memory_order_acquire);
    }

    void FinishNetSession(const std::string& reason, bool notifyEnded) {
        quit_.store(true);
        decCv_.notify_all();
        {
            std::lock_guard<std::mutex> lk(textMutex_);
            if (!reason.empty())
                endReason_ = reason;
            else if (endReason_.empty())
                endReason_ = "stopped";
        }
        phase_.store(ClientPhase::Ended, std::memory_order_release);
        finished_.store(true, std::memory_order_release);

        std::string finalReason;
        {
            std::lock_guard<std::mutex> lk(textMutex_);
            finalReason = endReason_;
        }
        if (notifyEnded && cfg_.onEnded) cfg_.onEnded(finalReason.c_str());
        if (cfg_.onFinished) cfg_.onFinished(finalReason.c_str());
        LOGI("[Client] Session ended.");
    }

    void NetThread() {
        bool everStreamed = false;
        int failStreak = 0;

        for (;;) {
            if (userStop_.load(std::memory_order_acquire)) {
                FinishNetSession("stopped", false);
                return;
            }

            sessionDone_.store(false, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lk(textMutex_);
                endReason_.clear();
            }

            deskhub::ClientPump pump(MakePumpCallbacks(), diag_);

            deskhub::ClientPumpConfig pcfg;
            pcfg.clientId = MakeClientId(cfg_.sourceId);
            pcfg.maxWidth = uint16_t(cfg_.screenW);
            pcfg.maxHeight = uint16_t(cfg_.screenH);
            pcfg.sourceId = cfg_.sourceId;
            pcfg.desiredFps = cfg_.desiredFps;
            pcfg.sendNacks = cfg_.sendNacks;
            pcfg.wantsAudio = cfg_.wantsAudio;
            pcfg.logLossRuns = cfg_.logLossRuns;
            pcfg.statusSeparator = cfg_.statusSeparator;
            pcfg.passcode = cfg_.passcode;
            pcfg.displayName = cfg_.displayName;
            pcfg.sessionKeyHex = cfg_.sessionKeyHex;
            pump.Start(pcfg, NowUs());

            std::vector<deskhub::InputEvent> batch;

            deskhub::RecordStream fileStream;
            FileUploadCallbacks fileCb;
            fileCb.send = [this](std::span<const uint8_t> message) {
                return SendFileMessage(sock_, cfg_.server, message);
            };
            fileCb.onProgress = [this](const deskhub::TransferProgress& progress) {
                std::lock_guard<std::mutex> lk(fileMutex_);
                fileProgress_ = progress;
            };
            fileCb.onFinished = [this](deskhub::FileSenderState state, deskhub::TransferReason) {
                std::lock_guard<std::mutex> lk(fileMutex_);
                fileState_ = state;
            };
            FileUpload upload(fileCb);
            fileUpload_.store(&upload, std::memory_order_release);
            const auto clearUpload = [&] {
                upload.LinkLost();
                fileUpload_.store(nullptr, std::memory_order_release);
            };

            ClientNetLoopHooks hooks;
            hooks.stopped = [this] {
                return userStop_.load(std::memory_order_acquire) ||
                       sessionDone_.load(std::memory_order_acquire);
            };
            hooks.onFile = [&upload, &fileStream](std::span<const uint8_t> datagram) {
                std::vector<std::vector<uint8_t>> messages;
                FeedFileDatagram(fileStream, datagram, messages);
                for (const std::vector<uint8_t>& message : messages) upload.HandleMessage(message);
            };
            hooks.afterFrames = [this](deskhub::ClientPump& p, uint64_t now) {
                if (decodeFailed_.exchange(false, std::memory_order_acq_rel))
                    p.RequestKeyframe("dec_fail", now);
                if (displayCongested_.exchange(false, std::memory_order_acq_rel))
                    p.RequestKeyframe("display_congested", now);
                if (queueOverflow_.exchange(false, std::memory_order_acq_rel))
                    p.RequestKeyframe("q_overflow", now);
            };
            hooks.beforeTick = [this, &batch, &upload](deskhub::ClientPump& p, uint64_t now) {
                SyncKeepAwakeHeld(true);
                input_.Drain(now, batch);
                for (const auto& e : batch) p.QueueInput(e);
                if (cfg_.alwaysFocused || input_.wantsFocus()) p.SetFocused(true);
                std::optional<std::string> clip;
                {
                    std::lock_guard<std::mutex> lk(clipMutex_);
                    clip.swap(pendingLocalClip_);
                }
                if (clip) p.QueueClipboard(*clip);
                upload.Pump();
            };
            hooks.onPhase = [this, &everStreamed, &failStreak](bool streaming) {
                phase_.store(streaming ? ClientPhase::Streaming : ClientPhase::Connecting,
                    std::memory_order_release);
                if (streaming) {
                    everStreamed = true;
                    failStreak = 0;
                }
            };
            hooks.onSocketError = [this] {
                LOGE("[Client] Socket error.");
                {
                    std::lock_guard<std::mutex> lk(textMutex_);
                    endReason_ = "socket error";
                }
                sessionDone_.store(true, std::memory_order_release);
            };

            RunClientNetLoop(sock_, pump, hooks);
            clearUpload();

            if (userStop_.load(std::memory_order_acquire)) {
                FinishNetSession("stopped", false);
                return;
            }

            std::string reason;
            {
                std::lock_guard<std::mutex> lk(textMutex_);
                if (endReason_.empty()) endReason_ = "stopped";
                reason = endReason_;
            }

            const bool tryAgain = everStreamed &&
                                  deskhub::IsTransientClientDisconnect(reason) &&
                                  failStreak < deskhub::kClientReconnectMaxAttempts;
            if (!tryAgain) {
                FinishNetSession(reason, true);
                return;
            }

            const uint64_t delayUs = deskhub::ClientReconnectBackoffUs(failStreak);
            ++failStreak;
            LOGW("[Client] Transient disconnect (%s); reconnect %d/%d in %" PRIu64 " ms",
                reason.c_str(), failStreak, deskhub::kClientReconnectMaxAttempts, delayUs / 1000);

            ClearDecodeQueue();
            rebuildDecoder_.store(true, std::memory_order_release);
            decodeFailed_.store(false, std::memory_order_release);
            displayCongested_.store(false, std::memory_order_release);
            queueOverflow_.store(false, std::memory_order_release);
            input_.ReleaseAll(NowUs());
            phase_.store(ClientPhase::Connecting, std::memory_order_release);
            PublishStatus(deskhub::ui::kReconnecting.get());

            if (!WaitReconnectBackoff(delayUs)) {
                FinishNetSession("stopped", false);
                return;
            }
            if (!OpenSocket()) {
                FinishNetSession("socket error", true);
                return;
            }
            LOGI("[Client] Reconnecting to %s (source %u) ...", cfg_.server.ToString().c_str(),
                cfg_.sourceId);
        }
    }

    ClientEngineConfig cfg_{};
    UdpSocket sock_;

    std::thread netThread_;
    std::thread decodeThread_;

    std::atomic<bool> quit_{false};
    std::atomic<bool> userStop_{false};
    std::atomic<bool> sessionDone_{false};
    std::atomic<bool> finished_{false};
    std::atomic<ClientPhase> phase_{ClientPhase::Idle};

    std::mutex textMutex_;
    std::string statusLine_;
    std::string endReason_;

    mutable std::mutex linkMutex_;
    deskhub::LinkPulseView linkView_{};

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

    std::mutex clipMutex_;
    std::optional<std::string> pendingLocalClip_;
    std::deque<std::string> remoteClips_;

    deskhub::ClientInputQueue input_;
    deskhub::diag::ClientDiag diag_;
    AudioPlayer player_;

    bool keepAwakeHeld_ = false;

    void SyncKeepAwakeHeld(bool sessionActive) {
        const bool want = sessionActive && ReadUiSettings().keepAwake;
        if (want == keepAwakeHeld_) return;
        if (want)
            AcquireKeepAwake();
        else
            ReleaseKeepAwake();
        keepAwakeHeld_ = want;
    }

    std::atomic<int64_t> lastE2eUs_{-1};
    deskhub::ClockOffset clockOffset_;

    std::atomic<FileUpload*> fileUpload_{nullptr};
    mutable std::mutex fileMutex_;
    deskhub::TransferProgress fileProgress_{};
    deskhub::FileSenderState fileState_ = deskhub::FileSenderState::Idle;
    std::string fileError_{};
};

}
