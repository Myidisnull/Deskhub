#pragma once
#include "deskhub/control/ClockOffset.h"
#include "deskhub/diag/ClientDiag.h"
#include "deskhub/input/ClientInputQueue.h"
#include "deskhub/media/VideoContract.h"
#include "deskhub/protocol/Wire.h"
#include "deskhub/session/ClientPump.h"
#include "deskhub/transport/Reassembler.h"
#include "deskhub/ui/Strings.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/diag/LogFile.h"
#include "deskhubp/net/ClientNetLoop.h"
#include "deskhubp/audio/AudioPlayer.h"
#include "deskhubp/net/SessionTransport.h"
#include "deskhubp/session/FileUpload.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/system/HostIdentity.h"
#include "deskhubp/system/KeepAwake.h"
#include "deskhubp/system/TrustStoreFile.h"
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
    Ended = 3,
    Deciding = 4 };

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
    std::string hostLabel;

    std::function<void(deskhub::TrustVerdict, std::string_view fingerprint)> onTrustAsked;
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
        if (!sock_.Connect(QuicSettings{}, cfg_.server, HostLabel())) {
            LOGE("[Client] Failed to open socket.");
            return false;
        }
        sock_.SetRecvTimeout(10);

        FileUploadCallbacks uploadHooks;
        uploadHooks.send = [this](std::span<const uint8_t> message) {
            return sock_.SendRecordOn(cfg_.server, kQuicFileStream, message);
        };
        upload_ = std::make_unique<FileUpload>(std::move(uploadHooks));

        sock_.SetOnStreamBroken([this](const NetAddr&, uint64_t stream) {
            if (stream == kQuicFileStream && upload_) upload_->LinkLost();
        });

        trustDecision_.store(int(TrustDecision::Pending), std::memory_order_release);
        autoTrustPending_.store(false, std::memory_order_release);

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
        if (cfg_.wantsAudio && !player_.Start())
            LOGW("[Client] Watching without sound: no audio device could be opened.");
        decodeThread_ = std::thread([this] { DecodeThread(); });
        netThread_ = std::thread([this] { NetThread(); });
        LOGI("[Client] Connecting to %s (source %u) ...", cfg_.server.ToString().c_str(),
            cfg_.sourceId);
        if (LoadUiSettings().keepAwake) {
            AcquireKeepAwake();
            keepAwakeHeld_ = true;
        }
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
        player_.Stop();
        quit_.store(true);
        decCv_.notify_all();
        surfaceCv_.notify_all();
        if (netThread_.joinable()) netThread_.join();
        if (decodeThread_.joinable()) decodeThread_.join();
        sock_.Close();
        if (keepAwakeHeld_) {
            ReleaseKeepAwake();
            keepAwakeHeld_ = false;
        }
    }

    void AcceptFingerprint() {
        trustDecision_.store(int(TrustDecision::Accepted), std::memory_order_release);
    }

    void RejectFingerprint() {
        trustDecision_.store(int(TrustDecision::Rejected), std::memory_order_release);
    }

    std::string Fingerprint() const {
        std::lock_guard<std::mutex> lk(textMutex_);
        return deskhub::IsZero(fingerprint_) ? std::string() : FormatFingerprint(fingerprint_);
    }

    deskhub::TrustVerdict Verdict() const {
        std::lock_guard<std::mutex> lk(textMutex_);
        return verdict_;
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

    AudioPlayer::Stats audioStats() const {
        return player_.stats();
    }

    bool audioRunning() const {
        return player_.running();
    }

    bool SendFiles(const std::vector<std::filesystem::path>& paths) {
        if (!upload_) return false;
        return upload_->Begin(paths);
    }

    bool uploading() const {
        return upload_ && upload_->Busy();
    }

    deskhub::FileSenderState uploadState() const {
        return upload_ ? upload_->State() : deskhub::FileSenderState::Idle;
    }

    deskhub::TransferProgress uploadProgress() const {
        return upload_ ? upload_->Progress() : deskhub::TransferProgress{};
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

    std::string HostLabel() const {
        return cfg_.hostLabel.empty() ? cfg_.server.ToString() : cfg_.hostLabel;
    }

    bool AwaitTrustedHost() {
        if (!sock_.WaitEstablished(cfg_.server, kHandshakeTimeoutMs)) {
            std::lock_guard<std::mutex> lk(textMutex_);
            endReason_ = deskhub::ui::kTerminalUnreachable;
            return false;
        }

        const std::optional<deskhub::Fingerprint> peer = sock_.PeerFingerprint(cfg_.server);
        if (!peer) {
            std::lock_guard<std::mutex> lk(textMutex_);
            endReason_ = deskhub::ui::kTerminalUnreachable;
            return false;
        }

        const std::string endpoint = cfg_.server.ToString();
        const deskhub::TrustVerdict verdict = CheckTrustedHost(endpoint, *peer);
        {
            std::lock_guard<std::mutex> lk(textMutex_);
            fingerprint_ = *peer;
            verdict_ = verdict;
        }
        if (verdict == deskhub::TrustVerdict::Trusted) return true;

        if (verdict == deskhub::TrustVerdict::Unknown) {
            autoTrustPending_.store(true, std::memory_order_release);
            return true;
        }

        phase_.store(ClientPhase::Deciding, std::memory_order_release);
        if (cfg_.onTrustAsked) cfg_.onTrustAsked(verdict, FormatFingerprint(*peer));

        while (!quit_.load()) {
            const TrustDecision decision =
                TrustDecision(trustDecision_.load(std::memory_order_acquire));
            if (decision == TrustDecision::Rejected) return false;
            if (decision == TrustDecision::Accepted) {
                RememberTrustedHost(endpoint, HostLabel(), *peer, NowUnixSeconds());
                phase_.store(ClientPhase::Connecting, std::memory_order_release);
                return true;
            }
            SleepUs(kTrustPollUs);
        }
        return false;
    }

    void RememberIfPasscodeProvedIt() {
        if (!autoTrustPending_.exchange(false, std::memory_order_acq_rel)) return;
        deskhub::Fingerprint peer;
        {
            std::lock_guard<std::mutex> lk(textMutex_);
            peer = fingerprint_;
            verdict_ = deskhub::TrustVerdict::Trusted;
        }
        if (deskhub::IsZero(peer)) return;
        RememberTrustedHost(cfg_.server.ToString(), HostLabel(), peer, NowUnixSeconds());
        LOGI("[Client] Passcode accepted \xE2\x80\x94 remembering %s as %s",
            cfg_.server.ToString().c_str(), FormatFingerprint(peer).c_str());
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
            RememberIfPasscodeProvedIt();
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

    bool ProveWhoWeAre() {
        ClientAuthConfig auth;
        auth.identity = LoadOrCreateHostIdentity(
            cfg_.displayName.empty() ? SessionDeviceName() : cfg_.displayName);
        auth.passcode = cfg_.passcode;
        auth.hostFingerprint = fingerprint_;
        auth.clientName = cfg_.displayName.empty() ? SessionDeviceName() : cfg_.displayName;

        deskhub::AuthResultCode code = deskhub::AuthResultCode::NotPaired;
        bool hostProved = false;
        const bool ok = sock_.RunClientAuth(cfg_.server, std::move(auth), kAuthTimeoutMs, code,
            hostProved);
        if (ok) {
            if (hostProved) RememberIfPasscodeProvedIt();
            return true;
        }

        std::lock_guard<std::mutex> lk(textMutex_);
        endReason_ = deskhub::ui::AuthRefusalText(code);
        return false;
    }

    void NetThread() {
        if (!AwaitTrustedHost() || !ProveWhoWeAre()) {
            quit_.store(true);
            phase_.store(ClientPhase::Ended, std::memory_order_release);
            finished_.store(true, std::memory_order_release);
            decCv_.notify_all();
            std::string reason;
            {
                std::lock_guard<std::mutex> lk(textMutex_);
                if (endReason_.empty()) endReason_ = deskhub::ui::kTrustReject;
                reason = endReason_;
            }
            if (cfg_.onEnded) cfg_.onEnded(reason.c_str());
            if (cfg_.onFinished) cfg_.onFinished(reason.c_str());
            return;
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
        pump.Start(pcfg, NowUs());

        std::vector<deskhub::InputEvent> batch;

        ClientNetLoopHooks hooks;
        hooks.stopped = [this] { return quit_.load(); };
        hooks.onFile = [this](std::span<const uint8_t> message) {
            if (upload_) upload_->HandleMessage(message);
        };
        hooks.pumpFiles = [this] {
            if (upload_) upload_->Pump();
        };
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
            std::optional<std::string> clip;
            {
                std::lock_guard<std::mutex> lk(clipMutex_);
                clip.swap(pendingLocalClip_);
            }
            if (clip) p.QueueClipboard(*clip);
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

        if (upload_) upload_->LinkLost();
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

    enum class TrustDecision : int32_t { Pending = 0,
        Accepted = 1,
        Rejected = 2 };

    static constexpr uint32_t kHandshakeTimeoutMs = 5'000;
    static constexpr uint32_t kAuthTimeoutMs = 65'000;
    static constexpr uint64_t kTrustPollUs = 20'000;

    ClientEngineConfig cfg_{};
    SessionTransport sock_;
    std::unique_ptr<FileUpload> upload_;
    std::atomic<int32_t> trustDecision_{0};
    std::atomic<bool> autoTrustPending_{false};
    deskhub::Fingerprint fingerprint_{};
    deskhub::TrustVerdict verdict_ = deskhub::TrustVerdict::Unknown;

    std::thread netThread_;
    std::thread decodeThread_;

    std::atomic<bool> quit_{false};
    std::atomic<bool> finished_{false};
    std::atomic<ClientPhase> phase_{ClientPhase::Idle};

    mutable std::mutex textMutex_;
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

    std::mutex clipMutex_;
    std::optional<std::string> pendingLocalClip_;
    std::deque<std::string> remoteClips_;

    deskhub::ClientInputQueue input_;
    deskhub::diag::ClientDiag diag_;
    AudioPlayer player_;

    std::atomic<int64_t> lastE2eUs_{-1};
    deskhub::ClockOffset clockOffset_;
    bool keepAwakeHeld_ = false;
};

}
