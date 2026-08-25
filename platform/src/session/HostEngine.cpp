#include "deskhubp/session/HostEngine.h"

#include "deskhub/net/BindAddress.h"
#include "deskhub/session/HostRouter.h"
#include "deskhub/session/HostSession.h"
#include "deskhub/ui/Brand.h"
#include "deskhub/ui/Strings.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/diag/StallLog.h"
#include "deskhubp/net/NetInfo.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/system/KeepAwake.h"
#include "deskhubp/system/UiSettingsStore.h"

#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <utility>

namespace deskhubp {
namespace {

std::string DefaultPortError(const UdpSocket& sock, uint16_t port) {
    return sock.lastBindAddrInUse()
               ? "UDP port " + std::to_string(port) +
                     " is already in use \xE2\x80\x94 another " +
                     std::string(deskhub::brand::kProductName) +
                     " is probably still "
                     "running. Close it and try again."
               : "Could not open UDP port " + std::to_string(port) + ".";
}

}

HostEngine::~HostEngine() {
    try {
        Stop();
    } catch (...) {
        std::fprintf(stderr, "[%s] [Agent] stop failed during shutdown\n",
            deskhub::brand::kLogLineTag);
    }
}

std::vector<deskhub::media::AgentSourceStatus> HostEngine::Status() {
    std::lock_guard<std::mutex> lk(statusMutex_);
    return statusRows_;
}

std::string HostEngine::LastError() {
    std::lock_guard<std::mutex> lk(errMutex_);
    return lastError_;
}

std::string HostEngine::BindWarning() {
    std::lock_guard<std::mutex> lk(errMutex_);
    return bindWarning_;
}

bool HostEngine::Fail(std::string message) {
    LOGE("[Agent] %s", message.c_str());
    std::lock_guard<std::mutex> lk(errMutex_);
    lastError_ = std::move(message);
    return false;
}

std::vector<HostSource*> HostEngine::AllSources() {
    std::vector<HostSource*> all;
    all.reserve(pipes_.size());
    for (auto& p : pipes_) all.push_back(p.get());
    return all;
}

void HostEngine::PublishStatus() {
    std::vector<deskhub::media::AgentSourceStatus> rows =
        PublishSourceStatus(live_, beacon_, policy_.status);
    std::lock_guard<std::mutex> lk(statusMutex_);
    statusRows_ = std::move(rows);
}

void HostEngine::AttachSession(HostSource& st) {
    st.offer.width = uint16_t(st.srcW.load());
    st.offer.height = uint16_t(st.srcH.load());
    st.offer.fps = uint8_t(opt_.fps);
    st.offer.bitrateBps = startBitrateBps_;
    LOGI("[Agent] Source %u \"%s\": %ux%u @%ufps, %u Mbps.", st.sourceId, st.name.c_str(),
        st.offer.width, st.offer.height, opt_.fps, opt_.bitrateMbps);

    if (policy_.source.attachInput) policy_.source.attachInput(st);

    HostSource* p = &st;
    HostSourcePolicy* sp = &policy_.source;

    HostSessionHooks hooks;
    hooks.fps = opt_.fps;
    hooks.send = [this, p](std::span<const uint8_t> d) {
        const uint64_t packed = p->replyPacked.load(std::memory_order_acquire);
        if (!packed) return;
        sock_.SendTo(NetAddr::Unpack(packed), d.data(), d.size());
    };
    hooks.sendToAddr = [this](uint64_t addrPacked, std::span<const uint8_t> d) {
        if (!addrPacked) return;
        sock_.SendTo(NetAddr::Unpack(addrPacked), d.data(), d.size());
    };
    hooks.sendToRequester = [this, p](std::span<const uint8_t> d) {
        const uint64_t packed = p->replyPacked.load(std::memory_order_acquire);
        if (!packed) return;
        sock_.SendTo(NetAddr::Unpack(packed), d.data(), d.size());
    };
    hooks.retarget = [p, sp] { return sp->retarget(*p); };
    hooks.applyInput = [this, p, sp](const deskhub::InputEvent& e) {
        if (!opt_.allowInput) return;
        sp->applyInput(*p, e);
    };
    hooks.releaseInput = [p, sp] { sp->releaseInput(*p); };
    hooks.applyClipboard = [this](std::string_view text) {
        std::lock_guard<std::mutex> lk(clipMutex_);
        remoteClips_.emplace_back(text);
        while (remoteClips_.size() > 4) remoteClips_.pop_front();
    };
    hooks.setEncoderBitrate = [p, sp](uint32_t bitrateBps) {
        return sp->setEncoderBitrate(*p, bitrateBps);
    };
    hooks.applyQualityStep = [p, sp](const deskhub::QualityStep& prev,
                                 const deskhub::QualityStep& next) {
        return sp->applyQualityStep(*p, prev, next);
    };

    const deskhub::HostCallbacks base = MakeHostCallbacks(st, std::move(hooks));
    deskhub::HostCallbacks cb = base;
    cb.onViewerLeave = [this, leave = base.onViewerLeave](uint64_t addrPacked, size_t viewerCount) {
        if (leave) leave(addrPacked, viewerCount);
        if (files_) files_->OnPeerGone(NetAddr::Unpack(addrPacked));
    };

    st.session = std::make_unique<deskhub::HostSession>(cb, st.offer, &viewerBudget_);
    st.session->SetPasscode(opt_.passcode);
    st.session->SetClipboardEnabled(opt_.clipboardSync);
    st.session->SetTrafficCipher(&st.traffic);
    st.session->SetEncryptRequired(opt_.encryptSession);
    if (opt_.encryptSession) {
        st.session->SetEscrowKey(opt_.escrowSessionKey);
        deskhub::crypto::KeyPair kp{};
        if (opt_.hostStaticSk.size() == deskhub::crypto::kKeySize) {
            std::memcpy(kp.sk, opt_.hostStaticSk.data(), deskhub::crypto::kKeySize);
            deskhub::crypto::PublicFromSecret(kp.pk, kp.sk);
            st.session->SetHostStaticKey(kp);
        }
        if (opt_.sessionKey.size() == deskhub::crypto::kKeySize)
            st.session->SetSessionKey(opt_.sessionKey.data());
    }
    st.netReady.store(true, std::memory_order_release);
}

void HostEngine::ShutdownSource(HostSource& st) {
    if (st.shutdownDone) return;
    st.shutdownDone = true;
    st.netReady.store(false);
    st.failed.store(true);
    {
        const uint64_t t0 = NowUs();
        StopAnrWatch watch("agent", "release_input");
        if (policy_.source.releaseInput) policy_.source.releaseInput(st);
        LogStopPhase("agent", "release_input", t0);
    }
    EndHostSession(st, sock_);
    {
        const uint64_t t0 = NowUs();
        StopAnrWatch watch("agent", "stop_capture");
        if (policy_.source.stopCapture) policy_.source.stopCapture(st);
        LogStopPhase("agent", "stop_capture", t0);
    }
}

bool HostEngine::Start(const std::vector<deskhub::media::ShareSource>& sources,
    const deskhub::media::AgentOptions& opt, HostEnginePolicy policy) {
    std::lock_guard<std::recursive_mutex> life(lifeMutex_);
    StopLocked();

    opt_ = opt;
    policy_ = std::move(policy);
    quit_.store(false);
    nextSourceId_ = 0;
    pipes_.clear();
    live_.clear();
    {
        std::lock_guard<std::mutex> lk(statusMutex_);
        statusRows_.clear();
    }

    if (sources.empty()) return Fail(policy_.noSourceError);
    if (sources.size() > deskhub::kMaxSources)
        return Fail("At most " + std::to_string(deskhub::kMaxSources) +
                    " sources can be shared at once.");

    if (policy_.preflight) {
        std::string err = policy_.preflight();
        if (!err.empty()) return Fail(std::move(err));
    }

    startBitrateBps_ = opt_.bitrateMbps * 1'000'000u;

    std::vector<std::string> localIps;
    for (const AdapterAddr& a : ListLocalIPv4()) localIps.push_back(a.ip);
    const deskhub::BindChoice chosen = deskhub::SelectBindAddress(opt_.bindIp, localIps);
    {
        std::lock_guard<std::mutex> lk(errMutex_);
        bindWarning_ = chosen.fellBack ? deskhub::ui::BindFallbackWarning(opt_.bindIp) : "";
    }
    if (chosen.fellBack)
        LOGW("[Agent] %s", deskhub::ui::BindFallbackWarning(opt_.bindIp).c_str());

    if (!sock_.Open(opt_.port, chosen.ip))
        return Fail(policy_.portError ? policy_.portError(sock_)
                                      : DefaultPortError(sock_, opt_.port));
    sock_.SetRecvTimeout(100);

    if (policy_.afterSocket) {
        std::string err = policy_.afterSocket();
        if (!err.empty()) {
            sock_.Close();
            return Fail(std::move(err));
        }
    }

    LogListeningAddresses(opt_.port, chosen.ip);

    for (const deskhub::media::ShareSource& s : sources) {
        std::unique_ptr<HostSource> p = policy_.source.create(s, nextSourceId_++);
        if (!p) continue;
        pipes_.push_back(std::move(p));
        policy_.source.startCapture(*pipes_.back());
    }

    const std::vector<HostSource*> all = AllSources();
    live_ = SelectLiveSources(all, policy_.status.closed, nullptr,
        [this](HostSource& st) { ShutdownSource(st); });
    if (live_.empty()) {
        sock_.Close();
        return Fail(policy_.noUsableSourceError);
    }

    for (HostSource* st : live_) AttachSession(*st);

    localInputMon_.Start();
    SyncKeepAwakeHeld(true);
    StartAudio();
    if (policy_.onSharing) policy_.onSharing();
    LOGI("[Agent] Sharing %zu source(s). Waiting for client...", live_.size());

    PublishStatus();
    {
        std::lock_guard<std::mutex> lk(errMutex_);
        lastError_.clear();
    }
    running_.store(true, std::memory_order_release);
    if (opt_.acceptFiles) {
        files_ = std::make_unique<FileHost>();
        if (!files_->Start(sock_, DefaultTransferDir(), FileHostCallbacks{})) {
            files_.reset();
            LOGW("[Agent] File receive is off: the save folder is not writable.");
        }
    }
    recvThread_ = std::thread([this] {
        RecvLoop();
        running_.store(false, std::memory_order_release);
    });
    return true;
}

void HostEngine::Stop() {
    std::lock_guard<std::recursive_mutex> life(lifeMutex_);
    StopLocked();
}

void HostEngine::StopLocked() {
    if (pipes_.empty() && !recvThread_.joinable()) return;

    const uint64_t tAll = NowUs();
    LOGI("[DIAG][agent] evt=stop_begin sources=%zu recv_joinable=%d", pipes_.size(),
        recvThread_.joinable() ? 1 : 0);

    if (policy_.stopAudioCapture) policy_.stopAudioCapture();
    audio_.Stop();

    if (files_) files_->Stop();
    files_.reset();

    quit_.store(true);
    {
        StopAnrWatch watch("agent", "recv_join");
        const uint64_t t0 = NowUs();
        if (recvThread_.joinable()) recvThread_.join();
        LogStopPhase("agent", "recv_join", t0);
    }
    running_.store(false, std::memory_order_release);

    {
        StopAnrWatch watch("agent", "local_input");
        const uint64_t t0 = NowUs();
        localInputMon_.Stop();
        LogStopPhase("agent", "local_input", t0);
    }

    SyncKeepAwakeHeld(false);

    for (auto& up : pipes_) {
        StopAnrWatch watch("agent", "source_shutdown");
        const uint64_t t0 = NowUs();
        const std::string name = up->name;
        ShutdownSource(*up);
        LogStopPhaseNamed("agent", "source_shutdown", name.c_str(), t0);
    }
    LogTransferTotals(AllSources());
    sock_.Close();

    live_.clear();
    pipes_.clear();
    LogStopPhase("agent", "stop_total", tAll);
}

void HostEngine::RequestStopSource(uint8_t sourceId) {
    std::lock_guard<std::mutex> lk(controlMutex_);
    pendingSourceStops_.push_back(sourceId);
}

void HostEngine::RequestKickViewer(uint8_t sourceId, uint64_t addrPacked) {
    if (!addrPacked) return;
    std::lock_guard<std::mutex> lk(controlMutex_);
    pendingViewerKicks_.emplace_back(sourceId, addrPacked);
}

HostSource* HostEngine::FindLiveSource(uint8_t sourceId) {
    for (HostSource* st : live_)
        if (st->sourceId == sourceId) return st;
    return nullptr;
}

void HostEngine::OfferLocalClipboard(std::string text) {
    if (!opt_.clipboardSync || !running()) return;
    std::lock_guard<std::mutex> lk(clipMutex_);
    pendingLocalClip_ = std::move(text);
}

std::optional<std::string> HostEngine::TakeRemoteClipboard() {
    std::lock_guard<std::mutex> lk(clipMutex_);
    if (remoteClips_.empty()) return std::nullopt;
    std::string text = std::move(remoteClips_.front());
    remoteClips_.pop_front();
    return text;
}

void HostEngine::DrainLocalClipboard() {
    std::optional<std::string> text;
    {
        std::lock_guard<std::mutex> lk(clipMutex_);
        text.swap(pendingLocalClip_);
    }
    if (!text) return;
    for (HostSource* st : live_) {
        if (st->failed.load() || !st->session) continue;
        st->clipOut.OfferLocal(*text);
    }
}

void HostEngine::DrainControlRequests() {
    std::vector<uint8_t> stops;
    std::vector<std::pair<uint8_t, uint64_t>> kicks;
    {
        std::lock_guard<std::mutex> lk(controlMutex_);
        stops.swap(pendingSourceStops_);
        kicks.swap(pendingViewerKicks_);
    }
    if (stops.empty() && kicks.empty()) return;

    for (const auto& [sourceId, addrPacked] : kicks) {
        HostSource* st = FindLiveSource(sourceId);
        if (!st || st->failed.load() || !st->session) continue;
        if (st->session->KickViewer(addrPacked))
            LOGI("[Agent][%s] Viewer %s disconnected by the host.", st->name.c_str(),
                NetAddr::Unpack(addrPacked).ToString().c_str());
    }

    for (const uint8_t sourceId : stops) {
        HostSource* st = FindLiveSource(sourceId);
        if (!st || st->shutdownDone) continue;
        LOGI("[Agent][%s] Sharing stopped by the host.", st->name.c_str());
        ShutdownSource(*st);
    }

    PublishStatus();
}

void HostEngine::SyncKeepAwakeHeld(bool sessionActive) {
    const bool want = sessionActive && ReadUiSettings().keepAwake;
    if (want == keepAwakeHeld_) return;
    if (want)
        AcquireKeepAwake();
    else
        ReleaseKeepAwake();
    keepAwakeHeld_ = want;
}

void HostEngine::StartAudio() {
    if (!opt_.audio) return;
    if (!policy_.startAudioCapture) {
        LOGW("[Agent] Sharing without sound: this build captures no audio.");
        return;
    }
    if (!audio_.Start([this](std::span<const uint8_t> frame, uint32_t seq, uint64_t timestampUs) {
            for (HostSource* st : live_) SendAudioFrame(*st, sock_, frame, seq, timestampUs);
        })) {
        LOGW("[Agent] Sharing without sound: the audio encoder did not start.");
        return;
    }
    if (!policy_.startAudioCapture(audio_.format(),
            [this](std::span<const int16_t> pcm) { audio_.Offer(pcm); })) {
        LOGW("[Agent] Sharing without sound: nothing to capture it from.");
        audio_.Stop();
    }
}

void HostEngine::RecvLoop() {
    const bool takingFiles = files_ != nullptr && files_->Running();
    beacon_.SetPasscode(opt_.passcode);
    beacon_.SetCaps(
        deskhub::HostCaps{opt_.allowInput, false, audio_.running(), takingFiles});

    HostNetLoopHooks loop;
    loop.fallbackFps = opt_.fps;
    loop.stopped = [this] { return quit_.load(); };
    loop.onTick = [this] {
        const bool filesOn = files_ != nullptr && files_->Running();
        beacon_.SetCaps(
            deskhub::HostCaps{opt_.allowInput, false, audio_.running(), filesOn});
        DrainControlRequests();
        DrainLocalClipboard();
        if (files_) files_->DrainGone();
        SyncKeepAwakeHeld(true);
    };
    loop.publishStatus = [this] { PublishStatus(); };
    loop.onFile = [this](const NetAddr& from, std::span<const uint8_t> datagram) {
        if (files_) files_->HandleDatagram(from, datagram);
    };
    loop.source.closed = policy_.status.closed;
    loop.source.zeroCopy = policy_.status.zeroCopy;
    loop.source.shutdown = [this](HostSource& st) { ShutdownSource(st); };
    loop.source.flush = policy_.source.flush;
    loop.source.inputSkipped = policy_.source.inputSkipped;
    loop.source.takeIdleFrames = policy_.source.takeIdleFrames;

    RunHostNetLoop(sock_, beacon_, live_, loop);
}

}
