#include "AgentLoop.h"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <functional>
#include <utility>

#include "deskhubp/Log.h"
#include "capture/PortalScreenCast.h"
#include "capture/ScreenCapture.h"
#include "deskhubp/Clock.h"
#include "deskhubp/LogFile.h"
#include "deskhubp/Random.h"
#include "encode/VaEncoder.h"
#include "input/InputInjector.h"
#include "input/LocalInputMonitor.h"
#include "net/NetInfo.h"
#include "deskhubp/UdpSocket.h"

#include "deskhub/control/BitrateController.h"
#include "deskhub/control/QualityLadder.h"
#include "deskhub/diag/AgentDiag.h"
#include "deskhub/control/StreamSize.h"
#include "deskhub/session/Beacon.h"
#include "deskhub/session/HostSession.h"
#include "deskhub/transport/Packetizer.h"
#include "deskhub/transport/RetransmitCache.h"

namespace {

inline constexpr uint32_t kMinEncodeW = 160;
inline constexpr uint32_t kMinEncodeH = 64;

struct SourcePipeline {
    SourcePipeline(uint32_t startBps, uint32_t minBps)
        : curBitrateBps(startBps), rate(startBps, minBps) {}

    uint8_t sourceId = 0;
    uint32_t nodeId = 0;
    std::string name;
    int32_t srcX = 0, srcY = 0;

    ScreenCapture capture;
    InputInjector injector;
    std::unique_ptr<deskhub::HostSession> session;
    deskhub::StreamParams offer;
    deskhub::Packetizer packetizer;

    deskhub::RetransmitCache retxCache;
    std::mutex retxMutex;

    std::atomic<uint32_t> srcW{0}, srcH{0};
    std::atomic<uint32_t> nativeW{0}, nativeH{0};
    std::atomic<uint32_t> wantW{0}, wantH{0};
    std::atomic<uint32_t> curFps{0};
    std::atomic<uint64_t> lastEncodeUs{0};
    std::atomic<bool> qualityChanged{false};
    std::atomic<bool> sizeChanged{false};
    std::atomic<bool> wantFec{false};
    std::atomic<uint32_t> curBitrateBps{0};
    std::atomic<bool> netReady{false};
    std::atomic<bool> failed{false};
    bool shutdownDone = false;
    std::atomic<bool> paused{false};
    std::atomic<bool> forceIdr{false};
    std::atomic<uint64_t> peerPacked{0};
    std::atomic<uint64_t> bytesSent{0}, framesSent{0};
    std::atomic<uint32_t> captured{0};
    std::atomic<uint32_t> nextFrameId{0};
    std::atomic<uint32_t> uiRttMs{0};
    std::atomic<uint32_t> uiLossPct{0};
    std::atomic<uint32_t> uiRecvKbps{0};
    std::atomic<bool> haveFeedback{false};

    std::mutex encMutex;
    std::unique_ptr<VaEncoder> encoder;
    std::function<bool(uint32_t, uint32_t)> ensureEncoderFn;

    uint32_t cliW = 0, cliH = 0;
    std::unique_ptr<deskhub::QualityLadder> ladder;
    deskhub::QualityStep step;

    std::atomic<uint64_t> lastFrameUs{0};
    uint64_t lastKeepaliveUs = 0;

    deskhub::BitrateController rate;

    deskhub::diag::SourceRate statRate;
    deskhub::diag::SourceRate::Window statWindow;

    deskhub::diag::SourceDiag diag{deskhub::diag::AgentDiagCaps{
        false, true}};

    void DiagEncode(const std::function<bool()>& doEncode, bool idr) {
        const uint64_t t0 = NowUs();
        const bool ok = doEncode();
        const uint32_t ms = uint32_t((NowUs() - t0) / 1000);
        diag.encMs.Add(ms);
        if (!ok) LOGW("[DIAG][%s] evt=enc_fail idr=%d ms=%u", name.c_str(), idr ? 1 : 0, ms);
    }
};

}

struct AgentLoop::Impl {
    AgentOptions opt;
    UdpSocket sock;
    std::thread recvThread;
    std::atomic<bool> quit{false};

    std::vector<std::unique_ptr<SourcePipeline>> pipes;
    std::vector<SourcePipeline*> live;

    uint8_t nextSourceId = 0;

    std::mutex statusMutex;
    std::vector<AgentSourceStatus> statusRows;

    LocalInputMonitor localInputMon;

    deskhub::Beacon beacon;

    uint32_t startBitrate = 0, minBitrate = 1'000'000;

    NetAddr replyAddr{};

    void RecvLoop();
    void StartPipeline(SourcePipeline* p, int portalFd);
    void AttachSession(SourcePipeline* p);
    void ShutdownPipeline(SourcePipeline* p);
    SourcePipeline* MakePipeline(const AgentSource& s);
    void PublishStatus();
};

AgentLoop::AgentLoop() = default;

AgentLoop::~AgentLoop() {
    Stop();
}

std::vector<AgentSourceStatus> AgentLoop::Status() {
    if (!impl_) return {};
    std::lock_guard<std::mutex> lk(impl_->statusMutex);
    return impl_->statusRows;
}

std::string AgentLoop::LastError() {
    std::lock_guard<std::mutex> lk(errMutex_);
    return lastError_;
}

SourcePipeline* AgentLoop::Impl::MakePipeline(const AgentSource& s) {
    auto p = std::make_unique<SourcePipeline>(startBitrate, minBitrate);
    p->sourceId = nextSourceId++;
    p->nodeId = s.nodeId;
    p->name = s.name;
    p->srcX = s.x;
    p->srcY = s.y;
    pipes.push_back(std::move(p));
    return pipes.back().get();
}

void AgentLoop::Impl::StartPipeline(SourcePipeline* p, int portalFd) {
    UdpSocket* sockPtr = &sock;

    auto onPacket = [p, sockPtr](const uint8_t* data, size_t size, uint64_t tsUs,
                        bool keyframe) {
        if (!p->session || p->session->state() != deskhub::HostSession::State::Streaming) return;
        {
            const uint64_t nowUs = NowUs();
            p->diag.encLatMs.Add(nowUs > tsUs ? uint32_t((nowUs - tsUs) / 1000) : 0);
        }
        const uint64_t pp = p->peerPacked.load(std::memory_order_acquire);
        if (!pp) return;
        const NetAddr peer = NetAddr::Unpack(pp);
        p->packetizer.SetSessionId(p->session->sessionId());
        p->packetizer.SetFecEnabled(p->wantFec.load(std::memory_order_relaxed));
        const uint64_t sendT0 = NowUs();
        const size_t pkts = p->packetizer.SendFrame(
            std::span<const uint8_t>(data, size), p->nextFrameId++, tsUs, keyframe,
            [p, sockPtr, &peer](std::span<const uint8_t> d) {
                if (sockPtr->SendTo(peer, d.data(), d.size()))
                    p->bytesSent.fetch_add(d.size(), std::memory_order_relaxed);
                else
                    p->diag.sendFail.Add();
                std::lock_guard<std::mutex> lk(p->retxMutex);
                p->retxCache.Store(d);
            });
        const uint32_t burstMs = uint32_t((NowUs() - sendT0) / 1000);
        p->diag.burstMs.Add(burstMs);
        if (pkts) p->framesSent.fetch_add(1, std::memory_order_relaxed);
        if (pkts && keyframe) {
            p->diag.idr.Add();
            p->diag.LatchIdr(uint64_t(size), uint32_t(pkts), burstMs);
        }
    };

    const uint32_t fps = opt.fps;
    auto ensureEncoder = [p, fps, onPacket](uint32_t w, uint32_t h) -> bool {
        if (p->encoder && p->encoder->IsOpen()) return true;
        EncoderConfig cfg;
        cfg.width = w;
        cfg.height = h;
        cfg.fps = p->curFps.load(std::memory_order_relaxed);
        if (!cfg.fps) cfg.fps = fps;
        cfg.bitrateBps = p->curBitrateBps.load(std::memory_order_relaxed);
        cfg.onPacket = onPacket;
        auto enc = std::make_unique<VaEncoder>();
        if (!enc->Init(cfg)) {
            LOGE("[Agent][%s] VA-API refused to start an encoder.", p->name.c_str());
            p->failed.store(true);
            return false;
        }
        p->encoder = std::move(enc);
        return true;
    };
    p->ensureEncoderFn = ensureEncoder;

    const uint32_t maxDim = opt.maxDim;
    auto onFrame = [p, ensureEncoder, maxDim](const LinuxFrameInfo& fi) {
        p->captured.fetch_add(1, std::memory_order_relaxed);
        if (p->failed.load()) return;
        if (!fi.width || !fi.height) return;

        p->nativeW.store(fi.width, std::memory_order_relaxed);
        p->nativeH.store(fi.height, std::memory_order_relaxed);

        uint32_t encW = p->wantW.load(std::memory_order_relaxed);
        uint32_t encH = p->wantH.load(std::memory_order_relaxed);
        if (!encW || !encH) {
            const deskhub::StreamSize t =
                deskhub::FitStreamSize(fi.width, fi.height, maxDim, 0, 0);
            encW = t.width;
            encH = t.height;
        }
        if (encW > fi.width) encW = fi.width;
        if (encH > fi.height) encH = fi.height;
        encW &= ~1u;
        encH &= ~1u;
        if (!encW || !encH) return;

        std::lock_guard<std::mutex> lk(p->encMutex);

        if (p->srcW.load() != encW || p->srcH.load() != encH) {
            if (p->srcW.load())
                LOGI("[Agent][%s] Encode size %ux%u -> %ux%u (source %ux%u), rebuilding encoder.",
                    p->name.c_str(), p->srcW.load(), p->srcH.load(), encW, encH, fi.width,
                    fi.height);
            p->srcW.store(encW);
            p->srcH.store(encH);
            p->encoder.reset();
            p->sizeChanged.store(true, std::memory_order_release);
        }

        if (encW < kMinEncodeW || encH < kMinEncodeH) {
            if (!p->paused.exchange(true, std::memory_order_acq_rel))
                LOGI("[Agent][%s] Source too small to encode (%ux%u) — paused.", p->name.c_str(),
                    encW, encH);
            return;
        }
        if (p->paused.exchange(false, std::memory_order_acq_rel))
            LOGI("[Agent][%s] Source back to %ux%u — resuming.", p->name.c_str(), encW, encH);

        if (const uint32_t gateFps = p->curFps.load(std::memory_order_relaxed)) {
            const uint64_t minGapUs = 1'000'000ull / gateFps;
            const uint64_t last = p->lastEncodeUs.load(std::memory_order_relaxed);
            if (last && fi.timestampUs > last && fi.timestampUs - last + 500 < minGapUs) return;
            p->lastEncodeUs.store(fi.timestampUs, std::memory_order_relaxed);
        }

        p->lastFrameUs.store(fi.timestampUs, std::memory_order_relaxed);

        if (!p->netReady.load(std::memory_order_acquire)) return;
        if (!ensureEncoder(encW, encH)) return;
        const bool idr = p->forceIdr.exchange(false);
        VaEncoder* enc = p->encoder.get();
        p->DiagEncode([enc, &fi, idr] { return enc->Encode(fi, fi.timestampUs, idr); }, idr);
    };

    if (!p->capture.Start(portalFd, p->nodeId, opt.fps, onFrame)) {
        LOGE("[Agent][%s] Failed to start capture — skipping this source.", p->name.c_str());
        p->failed.store(true);
    }
}

void AgentLoop::Impl::AttachSession(SourcePipeline* p) {
    p->offer.width = uint16_t(p->srcW.load());
    p->offer.height = uint16_t(p->srcH.load());
    p->offer.fps = uint8_t(opt.fps);
    p->offer.bitrateBps = startBitrate;
    LOGI("[Agent] Source %u \"%s\": %ux%u @%ufps, %u Mbps.", p->sourceId, p->name.c_str(),
        p->offer.width, p->offer.height, opt.fps, opt.bitrateMbps);

    p->injector.SetLocalMonitor(&localInputMon);
    p->injector.SetEnabled(p->injector.Init(p->srcX, p->srcY, p->nativeW.load(),
        p->nativeH.load(), opt.desktopX, opt.desktopY, opt.desktopW, opt.desktopH));

    UdpSocket* sockPtr = &sock;
    Impl* self = this;

    deskhub::HostCallbacks cb;
    cb.send = [sockPtr, self](std::span<const uint8_t> d) {
        sockPtr->SendTo(self->replyAddr, d.data(), d.size());
    };
    cb.randomBytes = [](std::span<uint8_t> out) {
        return RandomBytes(out.data(), out.size());
    };
    const AgentOptions& o = opt;
    auto retarget = [p, &o]() -> deskhub::StreamSize {
        const uint32_t nw = p->nativeW.load(std::memory_order_relaxed);
        const uint32_t nh = p->nativeH.load(std::memory_order_relaxed);
        if (!nw || !nh) return {0, 0};
        deskhub::StreamSize t = deskhub::FitStreamSize(nw, nh, o.maxDim, p->cliW, p->cliH);
        const uint32_t pct = p->step.scalePct ? p->step.scalePct : 100;
        if (pct < 100) {
            t.width = (t.width * pct / 100u) & ~1u;
            t.height = (t.height * pct / 100u) & ~1u;
        }
        if (!t.width || !t.height) return {0, 0};
        p->wantW.store(t.width, std::memory_order_relaxed);
        p->wantH.store(t.height, std::memory_order_relaxed);
        return t;
    };

    cb.onHello = [p, &o, retarget](const deskhub::Hello& h) {
        p->cliW = h.maxWidth;
        p->cliH = h.maxHeight;
        p->step = deskhub::QualityStep{};
        const deskhub::StreamSize t = retarget();
        if (!t.width || !t.height) return;
        p->ladder = std::make_unique<deskhub::QualityLadder>(uint16_t(t.width),
            uint16_t(t.height), uint8_t(o.fps));
        p->step = p->ladder->current();
        p->curFps.store(p->step.fps, std::memory_order_relaxed);
        p->offer.width = uint16_t(t.width);
        p->offer.height = uint16_t(t.height);
        p->offer.fps = uint8_t(p->step.fps);
        p->offer.bitrateBps = p->curBitrateBps.load(std::memory_order_relaxed);
        p->session->SetOffer(p->offer);
        LOGI("[Agent][%s] Quality ladder: ceiling %ux%u @%ufps, %d rung(s).", p->name.c_str(),
            t.width, t.height, p->step.fps, p->ladder->rungCount());
    };
    cb.onStart = [p] {
        p->forceIdr.store(true);
        LOGI("[Agent][%s] Client START — beginning video push.", p->name.c_str());
    };
    cb.onKeyframeRequest = [p] { p->forceIdr.store(true); };
    cb.onNack = [p, sockPtr](uint32_t frameId, std::span<const uint16_t> indices) {
        const uint64_t pp = p->peerPacked.load(std::memory_order_acquire);
        if (!pp) return;
        const NetAddr peer = NetAddr::Unpack(pp);
        std::lock_guard<std::mutex> lk(p->retxMutex);
        for (uint16_t idx : indices) {
            const auto d = p->retxCache.Find(frameId, idx);
            if (!d.empty()) sockPtr->SendTo(peer, d.data(), d.size());
        }
    };
    cb.onInput = [p](const deskhub::InputEvent& e) { p->injector.Apply(e); };
    cb.onFocus = [p](bool focused) {
        if (!focused) p->injector.ReleaseAll();
    };
    cb.onDisconnect = [p] {
        p->peerPacked.store(0, std::memory_order_release);
        p->injector.ReleaseAll();
        {
            std::lock_guard<std::mutex> lk(p->retxMutex);
            p->retxCache.Reset();
        }
        LOGI("[Agent][%s] Client left (BYE/timeout).", p->name.c_str());
    };
    cb.onFeedback = [p, retarget](const deskhub::Feedback& fb) {
        p->uiRttMs.store(fb.rttMs, std::memory_order_relaxed);
        p->uiLossPct.store(fb.lossPct, std::memory_order_relaxed);
        p->uiRecvKbps.store(fb.recvBitrateKbps, std::memory_order_relaxed);
        p->haveFeedback.store(true, std::memory_order_release);

        const deskhub::BitrateDecision d = p->rate.Update(fb, NowUs());

        if (d.fecToggled) {
            p->wantFec.store(d.fecEnabled, std::memory_order_relaxed);
            LOGI("[Agent][%s] FEC %s (loss %u%%).", p->name.c_str(), d.fecEnabled ? "on" : "off",
                fb.lossPct);
        }

        if (d.changeBitrate) {
            const uint32_t cur = p->rate.bitrateBps();
            std::lock_guard<std::mutex> lk(p->encMutex);
            if (p->encoder && p->encoder->SetBitrate(d.bitrateBps)) {
                p->rate.CommitBitrate(d.bitrateBps);
                p->curBitrateBps.store(d.bitrateBps, std::memory_order_relaxed);
                LOGI("[Agent][%s] Bitrate %.1f -> %.1f Mbps (loss %u%%, RTT %u ms)",
                    p->name.c_str(), cur / 1e6, d.bitrateBps / 1e6, fb.lossPct, fb.rttMs);
            }
        }

        if (!p->ladder) return;
        if (!p->ladder->Update(p->rate.bitrateBps(), NowUs())) return;
        const deskhub::QualityStep prev = p->step;
        p->step = p->ladder->current();
        p->curFps.store(p->step.fps, std::memory_order_relaxed);
        const deskhub::StreamSize t = retarget();
        if (prev.fps != p->step.fps) {
            std::lock_guard<std::mutex> lk(p->encMutex);
            p->encoder.reset();
        }
        LOGI("[Agent][%s] Quality %u%%@%ufps -> %u%%@%ufps (%ux%u, budget %.1f Mbps)",
            p->name.c_str(), prev.scalePct, prev.fps, p->step.scalePct, p->step.fps, t.width,
            t.height, p->rate.bitrateBps() / 1e6);
        p->qualityChanged.store(true, std::memory_order_release);
    };

    p->session = std::make_unique<deskhub::HostSession>(cb, p->offer);
    p->netReady.store(true, std::memory_order_release);
}

void AgentLoop::Impl::ShutdownPipeline(SourcePipeline* p) {
    if (p->shutdownDone) return;
    p->shutdownDone = true;
    p->injector.ReleaseAll();
    if (p->session && p->session->state() != deskhub::HostSession::State::Idle) {
        const uint64_t pp = p->peerPacked.load();
        if (pp) {
            uint8_t bye[deskhub::kCommonHeaderSize];
            const size_t bn = deskhub::BuildBye(bye, p->session->sessionId());
            if (bn) sock.SendTo(NetAddr::Unpack(pp), bye, bn);
        }
    }
    p->capture.Stop();
    {
        std::lock_guard<std::mutex> lk(p->encMutex);
        if (p->encoder) p->encoder->Finish();
    }
    p->netReady.store(false);
    p->failed.store(true);
}

void AgentLoop::Impl::PublishStatus() {
    std::vector<AgentSourceStatus> rows;
    std::vector<deskhub::SourceInfo> infos;
    for (SourcePipeline* p : live) {
        if (p->failed.load() || p->capture.Closed()) continue;
        const uint64_t peer = p->peerPacked.load(std::memory_order_relaxed);
        AgentSourceStatus r;
        r.sourceId = p->sourceId;
        r.name = p->name;
        r.width = p->srcW.load();
        r.height = p->srcH.load();
        r.viewerConnected = peer != 0;
        if (peer) r.viewerAddr = NetAddr::Unpack(peer).ToString();
        r.captureFps = p->statWindow.captureFps;
        r.sendFps = p->statWindow.sendFps;
        r.sendKbps = p->statWindow.sendKbps;
        r.rttMs = p->uiRttMs.load(std::memory_order_relaxed);
        r.zeroCopy = p->capture.usingDmaBuf();
        rows.push_back(std::move(r));

        deskhub::SourceInfo si;
        si.sourceId = p->sourceId;
        si.width = uint16_t(p->srcW.load());
        si.height = uint16_t(p->srcH.load());
        si.name = p->name;
        infos.push_back(std::move(si));
    }
    beacon.SetSources(infos);

    std::lock_guard<std::mutex> lk(statusMutex);
    statusRows = std::move(rows);
}

bool AgentLoop::Start(const std::vector<AgentSource>& sources, const AgentOptions& opt) {
    Stop();
    impl_ = std::make_unique<Impl>();
    Impl* im = impl_.get();
    im->opt = opt;

    auto fail = [this](std::string msg) {
        LOGE("[Agent] %s", msg.c_str());
        std::lock_guard<std::mutex> lk(errMutex_);
        lastError_ = std::move(msg);
        return false;
    };

    if (sources.empty()) return fail("No display to share.");
    if (sources.size() > deskhub::kMaxSources)
        return fail("At most " + std::to_string(deskhub::kMaxSources) +
                    " sources can be shared at once.");

    PortalScreenCast& portal = PortalScreenCast::Instance();
    if (!portal.isOpen())
        return fail("The screen-capture permission is gone — press Share again.");

    if (!im->sock.Open(kDeskhubPort))
        return fail("UDP port " + std::to_string(kDeskhubPort) +
                    " is not available — another Deskhub is probably still running.");
    im->sock.SetRecvTimeout(100);

    im->startBitrate = opt.bitrateMbps * 1'000'000u;
    im->minBitrate = 1'000'000u;

    LOGI("[Agent] Listening on UDP port %u. On the other machine, enter one of:",
        unsigned(kDeskhubPort));
    for (const auto& a : ListLocalIPv4()) LOGI("    %s    (%s)", a.ip.c_str(), a.name.c_str());

    for (const AgentSource& s : sources) im->StartPipeline(im->MakePipeline(s), portal.pipewireFd());

    for (int i = 0; i < 1000; ++i) {
        bool allKnown = true;
        for (auto& p : im->pipes)
            if (!p->failed.load() && !p->srcW.load() && !p->capture.Closed()) allKnown = false;
        if (allKnown) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    for (auto& p : im->pipes) {
        if (p->failed.load() || !p->srcW.load()) {
            if (!p->failed.load())
                LOGW("[Agent][%s] No frame within 10s — not sharing this source.",
                    p->name.c_str());
            im->ShutdownPipeline(p.get());
            continue;
        }
        im->live.push_back(p.get());
    }
    if (im->live.empty()) {
        im->sock.Close();
        return fail("No usable source — the compositor sent no frame.");
    }

    for (SourcePipeline* p : im->live) im->AttachSession(p);

    im->localInputMon.Start();
    LOGI("[Agent] Sharing %zu source(s). Waiting for client...", im->live.size());

    im->PublishStatus();
    {
        std::lock_guard<std::mutex> lk(errMutex_);
        lastError_.clear();
    }
    running_.store(true, std::memory_order_release);
    im->recvThread = std::thread([this, im] {
        im->RecvLoop();
        running_.store(false, std::memory_order_release);
    });
    return true;
}

void AgentLoop::Stop() {
    if (!impl_) return;
    Impl* im = impl_.get();
    im->quit.store(true);
    if (im->recvThread.joinable()) im->recvThread.join();
    running_.store(false, std::memory_order_release);

    im->localInputMon.Stop();

    uint64_t totalFrames = 0;
    double totalMB = 0;
    for (auto& up : im->pipes) {
        im->ShutdownPipeline(up.get());
        totalFrames += up->framesSent.load();
        totalMB += up->bytesSent.load() / 1e6;
    }
    im->sock.Close();
    LOGI("[Agent] Stopped. Total: %" PRIu64 " frames sent, %.2f MB.", totalFrames, totalMB);
    impl_.reset();
}

void AgentLoop::Impl::RecvLoop() {
    uint8_t buf[deskhub::kMaxDatagram];
    uint8_t beaconBuf[deskhub::kMaxDatagram];
    uint64_t lastStatUs = NowUs();
    deskhub::diag::AgentDiag loopDiag;
    char line[deskhub::diag::SourceDiag::kStatusBufBytes];

    while (!quit.load()) {
        bool anyAlive = false;
        for (SourcePipeline* p : live)
            if (!p->failed.load() && !p->capture.Closed()) anyAlive = true;
        if (!anyAlive) {
            LOGI("[Agent] No source left alive — session over.");
            break;
        }

        NetAddr from;
        const int n = sock.RecvFrom(buf, sizeof(buf), from);
        const uint64_t now = NowUs();
        if (n < 0) {
            LOGE("[Agent] Socket error — stopping.");
            break;
        }

        if (n > 0) {
            const auto span = std::span<const uint8_t>(buf, size_t(n));
            const auto h = deskhub::ParseCommonHeader(span);
            if (const size_t rn = beacon.Reply(beaconBuf, span); rn) {
                sock.SendTo(from, beaconBuf, rn);
            } else if (h) {
                replyAddr = from;
                SourcePipeline* dst = nullptr;
                if (h->type == deskhub::MsgType::Hello) {
                    const auto m = deskhub::ParseHello(deskhub::PayloadOf(span));
                    if (m)
                        for (SourcePipeline* p : live)
                            if (p->sourceId == m->sourceId) dst = p;
                } else if (h->sessionId) {
                    for (SourcePipeline* p : live)
                        if (p->session && p->session->sessionId() == h->sessionId) dst = p;
                }
                if (dst && !dst->failed.load() && dst->session->HandlePacket(span, now)) {
                    const uint64_t pk = from.Pack();
                    if (dst->peerPacked.load(std::memory_order_relaxed) != pk) {
                        dst->peerPacked.store(pk, std::memory_order_release);
                        LOGI("[Agent][%s] Peer: %s", dst->name.c_str(), from.ToString().c_str());
                    }
                }
            }
        }

        for (SourcePipeline* p : live) {
            if (p->failed.load()) continue;
            if (p->capture.Closed() && !p->shutdownDone) {
                LOGI("[Agent][%s] Source closed — stopping this source.", p->name.c_str());
                ShutdownPipeline(p);
                PublishStatus();
                continue;
            }
            p->session->Tick(now);

            if (const char* idrLine = p->diag.FormatIdr(line, sizeof(line), p->name.c_str()))
                LOGI("%s", idrLine);

            const bool sized = p->sizeChanged.exchange(false, std::memory_order_acq_rel);
            const bool qual = p->qualityChanged.exchange(false, std::memory_order_acq_rel);
            if (!p->paused.load(std::memory_order_acquire) && (sized || qual)) {
                p->offer.width = uint16_t(p->srcW.load());
                p->offer.height = uint16_t(p->srcH.load());
                p->offer.fps = uint8_t(p->step.fps ? p->step.fps : opt.fps);
                p->offer.bitrateBps = p->curBitrateBps.load(std::memory_order_relaxed);
                p->session->SetOffer(p->offer);
                const uint64_t pp = p->peerPacked.load(std::memory_order_acquire);
                if (pp && p->session->state() == deskhub::HostSession::State::Streaming) {
                    deskhub::Reconfig rc{p->offer.width, p->offer.height, p->offer.bitrateBps,
                        p->offer.fps};
                    uint8_t rbuf[deskhub::kMaxDatagram];
                    const size_t rn = deskhub::BuildReconfig(rbuf, p->session->sessionId(), rc);
                    if (rn) sock.SendTo(NetAddr::Unpack(pp), rbuf, rn);
                    p->forceIdr.store(true);
                }
            }

            const uint64_t sinceFrameUs = now - p->lastFrameUs.load(std::memory_order_relaxed);
            const bool wantIdrFlush = p->forceIdr.load() && sinceFrameUs > 200'000;
            const bool wantKeepalive = sinceFrameUs > 500'000 &&
                                       now - p->lastKeepaliveUs >= 500'000;
            if (p->session->state() == deskhub::HostSession::State::Streaming &&
                (wantIdrFlush || wantKeepalive)) {
                std::lock_guard<std::mutex> lk(p->encMutex);
                if (p->encoder && p->encoder->haveSourceFrame()) {
                    const bool idr = p->forceIdr.exchange(false);
                    VaEncoder* enc = p->encoder.get();
                    p->DiagEncode([enc, now, idr] { return enc->EncodeLast(now, idr); }, idr);
                    p->lastKeepaliveUs = now;
                }
            }
        }

        if (now - lastStatUs >= 1'000'000) {
            const std::string hms = deskhubp::LocalTimeHms();
            for (SourcePipeline* p : live) {
                if (p->failed.load()) continue;
                const uint32_t cap = p->captured.load();
                const uint64_t by = p->bytesSent.load(), fr = p->framesSent.load();
                const auto& ist = p->session->inputStats();
                p->statWindow = p->statRate.Close(cap, fr, by, now);
                deskhub::diag::SourceDiag::Window sw;
                sw.rate = p->statWindow;
                sw.inputApplied = ist.applied;
                sw.inputLost = ist.lost;
                sw.inputSkipped = p->injector.skipped();

                deskhub::diag::SourceDiag::LinkView link;
                link.have = p->haveFeedback.load(std::memory_order_acquire);
                link.lossPct = p->uiLossPct.load(std::memory_order_relaxed);
                link.rttMs = p->uiRttMs.load(std::memory_order_relaxed);
                link.recvKbps = p->uiRecvKbps.load(std::memory_order_relaxed);

                LOGI("%s", deskhub::diag::SourceDiag::FormatStatus(line, sizeof(line), hms.c_str(),
                               p->name.c_str(), deskhub::diag::StateName(p->session->state()), sw, link));
                LOGI("%s", p->diag.FormatSum(line, sizeof(line), hms.c_str(), p->name.c_str(),
                               0, p->capture.usingDmaBuf()));
            }
            LOGI("%s", loopDiag.FormatSum(line, sizeof(line), hms.c_str()));
            PublishStatus();
            lastStatUs = now;
        }

        const uint32_t busyMs = uint32_t((NowUs() - now) / 1000);
        loopDiag.loopBusyMs.Add(busyMs);
        if (busyMs > 250) LOGW("[DIAG][agent] evt=recv_stall busy_ms=%u", busyMs);
    }
}
