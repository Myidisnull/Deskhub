#include "deskhubp/session/HostEngine.h"

#include "deskhub/net/BindAddress.h"
#include "deskhub/session/HostRouter.h"
#include "deskhub/session/HostSession.h"
#include "deskhub/ui/Strings.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/net/NetInfo.h"
#include "deskhubp/system/DeviceName.h"
#include "deskhubp/system/HostIdentity.h"
#include "deskhubp/system/PairedDevicesFile.h"
#include "deskhubp/system/KeepAwake.h"
#include "deskhubp/system/UiSettingsStore.h"

#include <cstdio>
#include <span>
#include <utility>

namespace deskhubp {
namespace {

std::string DefaultPortError(const SessionTransport& sock, uint16_t port) {
    return sock.lastBindAddrInUse()
               ? "UDP port " + std::to_string(port) +
                     " is already in use \xE2\x80\x94 another Deskhub is probably still "
                     "running. Close it and try again."
               : "Could not open UDP port " + std::to_string(port) + ".";
}

}

HostEngine::~HostEngine() {
    try {
        Stop();
    } catch (...) {
        std::fputs("[Deskhub] [Agent] stop failed during shutdown\n", stderr);
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

    const deskhub::HostCallbacks cb = MakeHostCallbacks(st, std::move(hooks));

    st.session = std::make_unique<deskhub::HostSession>(cb, st.offer, &viewerBudget_);
    st.session->SetConnectionAuthenticated(true);
    st.session->SetClipboardEnabled(opt_.clipboardSync);
    st.netReady.store(true, std::memory_order_release);
}

void HostEngine::ShutdownSource(HostSource& st) {
    if (st.shutdownDone) return;
    st.shutdownDone = true;
    if (policy_.source.releaseInput) policy_.source.releaseInput(st);
    EndHostSession(st, sock_);
    if (policy_.source.stopCapture) policy_.source.stopCapture(st);
    st.netReady.store(false);
    st.failed.store(true);
}

bool HostEngine::Start(const std::vector<deskhub::media::ShareSource>& sources,
    const deskhub::media::AgentOptions& opt, HostEnginePolicy policy) {
    Stop();

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

    const std::string commonName =
        opt_.deviceName.empty() ? LocalDeviceName() : opt_.deviceName;
    const HostIdentity identity = LoadOrCreateHostIdentity(commonName);
    if (!identity.Valid()) return Fail(std::string(deskhub::ui::kShareNoHostIdentity));

    QuicSettings settings;
    settings.certPemPath = identity.certPath;
    settings.keyPemPath = identity.keyPath;
    if (!sock_.Listen(settings, opt_.port, chosen.ip))
        return Fail(policy_.portError ? policy_.portError(sock_)
                                      : DefaultPortError(sock_, opt_.port));
    sock_.SetRecvTimeout(100);
    LOGI("[Agent] Host identity %s", deskhub::FormatFingerprint(identity.fingerprint).c_str());

    HostAuthConfig auth;
    auth.identity = identity;
    auth.SetPasscode(LoadOrCreateAuthSalt(), opt_.passcode);
    auth.allowNewPairings = opt_.allowNewPairings;
    TransportAuthCallbacks authHooks;
    authHooks.onPaired = [this](const NetAddr&, const deskhub::Fingerprint&,
                             std::string_view name) {
        LOGI("[Agent] %s is paired with this machine.", std::string(name).c_str());
        if (policy_.onPaired) policy_.onPaired();
    };
    authHooks.onApprovalNeeded = [this](const NetAddr& peer, const deskhub::Fingerprint& fp,
                                     std::string_view name) {
        if (policy_.onApprovalNeeded)
            policy_.onApprovalNeeded(peer.Pack(), deskhub::ShortFingerprint(fp),
                std::string(name));
    };
    authHooks.onRefused = [](const NetAddr& peer, deskhub::AuthResultCode) {
        LOGW("[Agent] %s was refused.", peer.ToString().c_str());
    };
    sock_.SetHostAuth(std::move(auth), std::move(authHooks));

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
    if (policy_.onSharing) policy_.onSharing();
    LOGI("[Agent] Sharing %zu source(s). Waiting for client...", live_.size());

    PublishStatus();
    {
        std::lock_guard<std::mutex> lk(errMutex_);
        lastError_.clear();
    }
    running_.store(true, std::memory_order_release);
    recvThread_ = std::thread([this] {
        RecvLoop();
        running_.store(false, std::memory_order_release);
    });
    if (LoadUiSettings().keepAwake) {
        AcquireKeepAwake();
        keepAwakeHeld_ = true;
    }
    return true;
}

void HostEngine::Stop() {
    if (pipes_.empty() && !recvThread_.joinable()) return;

    quit_.store(true);
    if (recvThread_.joinable()) recvThread_.join();
    running_.store(false, std::memory_order_release);

    localInputMon_.Stop();

    for (auto& up : pipes_) ShutdownSource(*up);
    LogTransferTotals(AllSources());
    sock_.Close();

    live_.clear();
    pipes_.clear();

    if (keepAwakeHeld_) {
        ReleaseKeepAwake();
        keepAwakeHeld_ = false;
    }
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

void HostEngine::AnswerPairingRequest(uint64_t addrPacked, bool allowed) {
    if (!addrPacked) return;
    std::lock_guard<std::mutex> lk(controlMutex_);
    pendingPairAnswers_.emplace_back(addrPacked, allowed);
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
    std::vector<std::pair<uint64_t, bool>> pairAnswers;
    {
        std::lock_guard<std::mutex> lk(controlMutex_);
        stops.swap(pendingSourceStops_);
        kicks.swap(pendingViewerKicks_);
        pairAnswers.swap(pendingPairAnswers_);
    }

    // The person at this machine answered a pairing request. It has to happen on
    // this thread because that is where the QUIC connection lives.
    for (const auto& [addrPacked, allowed] : pairAnswers)
        sock_.ApproveConnection(NetAddr::Unpack(addrPacked), allowed);

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

void HostEngine::RecvLoop() {
    beacon_.SetPasscode(opt_.passcode);

    HostNetLoopHooks loop;
    loop.fallbackFps = opt_.fps;
    loop.stopped = [this] { return quit_.load(); };
    loop.onTick = [this] {
        DrainControlRequests();
        DrainLocalClipboard();
    };
    loop.publishStatus = [this] { PublishStatus(); };
    loop.source.closed = policy_.status.closed;
    loop.source.zeroCopy = policy_.status.zeroCopy;
    loop.source.shutdown = [this](HostSource& st) { ShutdownSource(st); };
    loop.source.flush = policy_.source.flush;
    loop.source.inputSkipped = policy_.source.inputSkipped;
    loop.source.takeIdleFrames = policy_.source.takeIdleFrames;

    RunHostNetLoop(sock_, beacon_, live_, loop);
}

}
