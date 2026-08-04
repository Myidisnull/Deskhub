#include "deskhubp/session/HostEngine.h"

#include "deskhub/session/HostRouter.h"
#include "deskhub/session/HostSession.h"
#include "deskhubp/diag/Log.h"

#include <cstdio>
#include <span>
#include <utility>

namespace deskhubp {
namespace {

std::string DefaultPortError(const UdpSocket& sock, uint16_t port) {
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
    hooks.sendToPeer = [this, p](std::span<const uint8_t> d) {
        sock_.SendTo(NetAddr::Unpack(p->peerPacked.load(std::memory_order_acquire)), d.data(),
            d.size());
    };
    hooks.retarget = [p, sp] { return sp->retarget(*p); };
    hooks.applyInput = [p, sp](const deskhub::InputEvent& e) { sp->applyInput(*p, e); };
    hooks.releaseInput = [p, sp] { sp->releaseInput(*p); };
    hooks.setEncoderBitrate = [p, sp](uint32_t bitrateBps) {
        return sp->setEncoderBitrate(*p, bitrateBps);
    };
    hooks.applyQualityStep = [p, sp](const deskhub::QualityStep& prev,
                                 const deskhub::QualityStep& next) {
        return sp->applyQualityStep(*p, prev, next);
    };

    const deskhub::HostCallbacks cb = MakeHostCallbacks(st, std::move(hooks));

    st.session = std::make_unique<deskhub::HostSession>(cb, st.offer);
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

    if (!sock_.Open(opt_.port))
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

    LogListeningAddresses(opt_.port);

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
}

void HostEngine::RecvLoop() {
    HostNetLoopHooks loop;
    loop.fallbackFps = opt_.fps;
    loop.stopped = [this] { return quit_.load(); };
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
