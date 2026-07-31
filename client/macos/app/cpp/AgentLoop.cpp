#include "AgentLoop.h"

#include <CoreVideo/CVPixelBuffer.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <functional>
#include <utility>

#include "Permissions.h"
#include "capture/ScreenCapture.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/diag/LogFile.h"
#include "deskhubp/input/LocalInput.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/session/HostAgent.h"
#include "deskhubp/session/HostNetLoop.h"
#include "deskhubp/system/Clock.h"
#include "encode/VtEncoder.h"
#include "input/InputInjector.h"

#include "deskhub/control/QualityLadder.h"
#include "deskhub/diag/AgentDiag.h"
#include "deskhub/session/Beacon.h"
#include "deskhub/session/HostRouter.h"
#include "deskhub/session/HostSession.h"
#include "deskhub/session/SourcePipelineState.h"

namespace {

struct SourcePipeline : deskhub::SourcePipelineState {
    SourcePipeline(uint32_t startBps, uint32_t minBps)
        : deskhub::SourcePipelineState(startBps, minBps,
              deskhub::diag::AgentDiagCaps{true, false}) {}

    ~SourcePipeline() {
        if (cachedPb) CVPixelBufferRelease(static_cast<CVPixelBufferRef>(cachedPb));
    }

    uint32_t displayId = 0;

    ScreenCapture capture;
    InputInjector injector;

    std::mutex encMutex;
    std::unique_ptr<VtEncoder> encoder;
    std::function<bool(uint32_t, uint32_t)> ensureEncoderFn;

    void* cachedPb = nullptr;
    std::atomic<bool> haveCached{false};

    void DiagEncode(VtEncoder* enc, void* pb, uint64_t tsUs, bool idr) {
        const uint64_t t0 = NowUs();
        const bool ok = enc->Encode(pb, tsUs, idr);
        const uint32_t ms = uint32_t((NowUs() - t0) / 1000);
        diag.encMs.Add(ms);
        if (!ok) LOGW("[DIAG][%s] evt=enc_fail idr=%d ms=%u", name.c_str(), idr ? 1 : 0, ms);
    }
};

SourcePipeline& Pipeline(deskhub::SourcePipelineState& st) {
    return static_cast<SourcePipeline&>(st);
}

const SourcePipeline& Pipeline(const deskhub::SourcePipelineState& st) {
    return static_cast<const SourcePipeline&>(st);
}

}

struct AgentLoop::Impl {
    AgentOptions opt;
    UdpSocket sock;
    std::thread recvThread;
    std::atomic<bool> quit{false};

    std::vector<std::unique_ptr<SourcePipeline>> pipes;
    std::vector<deskhub::SourcePipelineState*> live;

    uint8_t nextSourceId = 0;

    std::mutex statusMutex;
    std::vector<AgentSourceStatus> statusRows;

    LocalInputMonitor localInputMon;

    deskhub::Beacon beacon;

    uint32_t startBitrate = 0, minBitrate = 1'000'000;

    NetAddr replyAddr{};

    void RecvLoop();
    void StartPipeline(SourcePipeline* p);
    void AttachSession(SourcePipeline* p);
    void ShutdownPipeline(SourcePipeline* p);
    SourcePipeline* MakePipeline(const AgentSource& s);
    void PublishStatus();
    std::vector<deskhub::SourcePipelineState*> AllStates();
    deskhubp::SourceStatusHooks StatusHooks();
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

std::vector<deskhub::SourcePipelineState*> AgentLoop::Impl::AllStates() {
    std::vector<deskhub::SourcePipelineState*> all;
    all.reserve(pipes.size());
    for (auto& p : pipes) all.push_back(p.get());
    return all;
}

deskhubp::SourceStatusHooks AgentLoop::Impl::StatusHooks() {
    deskhubp::SourceStatusHooks hooks;
    hooks.closed = [](const deskhub::SourcePipelineState& st) {
        return Pipeline(st).capture.Closed();
    };
    return hooks;
}

SourcePipeline* AgentLoop::Impl::MakePipeline(const AgentSource& s) {
    auto p = std::make_unique<SourcePipeline>(startBitrate, minBitrate);
    p->sourceId = nextSourceId++;
    p->displayId = uint32_t(s.targetId);
    p->name = s.name;
    pipes.push_back(std::move(p));
    return pipes.back().get();
}

void AgentLoop::Impl::StartPipeline(SourcePipeline* p) {
    UdpSocket* sockPtr = &sock;

    auto onPacket = [p, sockPtr](const uint8_t* data, size_t size, uint64_t tsUs,
                        bool keyframe) {
        deskhubp::SendEncodedFrame(*p, *sockPtr, std::span<const uint8_t>(data, size), tsUs,
            keyframe);
    };

    const uint32_t fps = opt.fps;
    p->curFps.store(fps, std::memory_order_relaxed);
    auto ensureEncoder = [p, fps, onPacket](uint32_t w, uint32_t h) -> bool {
        if (p->encoder && p->encoder->IsOpen()) return true;
        EncoderConfig cfg;
        cfg.width = w;
        cfg.height = h;
        cfg.fps = p->curFps.load(std::memory_order_relaxed);
        if (!cfg.fps) cfg.fps = fps;
        cfg.bitrateBps = p->curBitrateBps.load(std::memory_order_relaxed);
        cfg.onPacket = onPacket;
        auto enc = std::make_unique<VtEncoder>();
        if (!enc->Init(cfg)) {
            LOGE("[Agent][%s] VideoToolbox refused to start an encoder.", p->name.c_str());
            p->failed.store(true);
            return false;
        }
        p->encoder = std::move(enc);
        return true;
    };
    p->ensureEncoderFn = ensureEncoder;

    auto onFrame = [p, ensureEncoder](const MacFrameInfo& fi) {
        p->captured.fetch_add(1, std::memory_order_relaxed);
        if (p->failed.load()) return;
        const uint32_t encW = fi.meta.width & ~1u, encH = fi.meta.height & ~1u;
        if (!encW || !encH) return;

        std::lock_guard<std::mutex> lk(p->encMutex);

        if (p->srcW.load() != encW || p->srcH.load() != encH) {
            if (p->srcW.load())
                LOGI("[Agent][%s] Source resized %ux%u -> %ux%u, rebuilding encoder.",
                    p->name.c_str(), p->srcW.load(), p->srcH.load(), encW, encH);
            p->srcW.store(encW);
            p->srcH.store(encH);
            p->encoder.reset();
            if (p->cachedPb) {
                CVPixelBufferRelease(static_cast<CVPixelBufferRef>(p->cachedPb));
                p->cachedPb = nullptr;
            }
            p->haveCached.store(false, std::memory_order_release);
            p->sizeChanged.store(true, std::memory_order_release);
        }

        if (encW < deskhub::kMinEncodeWidth || encH < deskhub::kMinEncodeHeight) {
            if (!p->paused.exchange(true, std::memory_order_acq_rel))
                LOGI(
                    "[Agent][%s] Source too small to encode (%ux%u) — paused, "
                    "waiting for it to grow back.",
                    p->name.c_str(), encW, encH);
            return;
        }
        if (p->paused.exchange(false, std::memory_order_acq_rel))
            LOGI("[Agent][%s] Source back to %ux%u — resuming.", p->name.c_str(), encW, encH);

        if (p->cachedPb) CVPixelBufferRelease(static_cast<CVPixelBufferRef>(p->cachedPb));
        p->cachedPb = CVPixelBufferRetain(static_cast<CVPixelBufferRef>(fi.pixelBuffer));
        p->haveCached.store(p->cachedPb != nullptr, std::memory_order_release);
        p->lastFrameUs.store(fi.meta.timestampUs, std::memory_order_relaxed);

        if (!p->netReady.load(std::memory_order_acquire)) return;
        if (!ensureEncoder(encW, encH)) return;
        p->DiagEncode(p->encoder.get(), fi.pixelBuffer, fi.meta.timestampUs,
            p->forceIdr.exchange(false));
    };

    if (!p->capture.Start(p->displayId,
            deskhub::media::CaptureOptions{opt.fps, opt.maxDim}, onFrame)) {
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
    p->injector.SetEnabled(p->injector.Init(p->displayId));

    UdpSocket* sockPtr = &sock;
    Impl* self = this;

    deskhubp::HostSessionHooks hooks;
    hooks.fps = opt.fps;
    hooks.send = [sockPtr, self](std::span<const uint8_t> d) {
        sockPtr->SendTo(self->replyAddr, d.data(), d.size());
    };
    hooks.sendToPeer = [p, sockPtr](std::span<const uint8_t> d) {
        sockPtr->SendTo(NetAddr::Unpack(p->peerPacked.load(std::memory_order_acquire)), d.data(),
            d.size());
    };
    hooks.retarget = [p] {
        uint32_t w = 0, h = 0;
        p->capture.SetClientSize(uint16_t(p->cliW), uint16_t(p->cliH), w, h);
        return deskhub::StreamSize{w, h};
    };
    hooks.applyInput = [p](const deskhub::InputEvent& e) { p->injector.Apply(e); };
    hooks.releaseInput = [p] { p->injector.ReleaseAll(); };
    hooks.setEncoderBitrate = [p](uint32_t bitrateBps) {
        std::lock_guard<std::mutex> lk(p->encMutex);
        return p->encoder && p->encoder->SetBitrate(bitrateBps);
    };
    hooks.applyQualityStep = [p](const deskhub::QualityStep&,
                                 const deskhub::QualityStep& next) {
        {
            std::lock_guard<std::mutex> lk(p->encMutex);
            if (p->encoder) p->encoder->SetFps(next.fps);
        }
        uint32_t w = 0, h = 0;
        p->capture.SetQuality(next.scalePct, next.fps, w, h);
        return deskhub::StreamSize{w, h};
    };

    const deskhub::HostCallbacks cb = deskhubp::MakeHostCallbacks(*p, std::move(hooks));

    p->session = std::make_unique<deskhub::HostSession>(cb, p->offer);
    p->netReady.store(true, std::memory_order_release);
}

void AgentLoop::Impl::ShutdownPipeline(SourcePipeline* p) {
    if (p->shutdownDone) return;
    p->shutdownDone = true;
    p->injector.ReleaseAll();
    deskhubp::EndHostSession(*p, sock);
    p->capture.Stop();
    {
        std::lock_guard<std::mutex> lk(p->encMutex);
        if (p->encoder) p->encoder->Finish();
        if (p->cachedPb) {
            CVPixelBufferRelease(static_cast<CVPixelBufferRef>(p->cachedPb));
            p->cachedPb = nullptr;
            p->haveCached.store(false, std::memory_order_release);
        }
    }
    p->netReady.store(false);
    p->failed.store(true);
}

void AgentLoop::Impl::PublishStatus() {
    std::vector<AgentSourceStatus> rows =
        deskhubp::PublishSourceStatus(live, beacon, StatusHooks());
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

    if (!macperm::HasScreenRecording())
        LOGW(
            "[Agent] Screen Recording permission not detected — "
            "capture will likely fail. Grant it in System Settings and restart.");

    if (!im->sock.Open(kDeskhubPort))
        return fail("UDP port " + std::to_string(kDeskhubPort) +
                    " is not available — another Deskhub is probably still running.");
    im->sock.SetRecvTimeout(100);

    im->startBitrate = opt.bitrateMbps * 1'000'000u;
    im->minBitrate = 1'000'000u;

    deskhubp::LogListeningAddresses();

    for (const AgentSource& s : sources) im->StartPipeline(im->MakePipeline(s));

    const std::vector<deskhub::SourcePipelineState*> all = im->AllStates();
    im->live = deskhubp::SelectLiveSources(all, im->StatusHooks().closed, nullptr,
        [im](deskhub::SourcePipelineState& st) { im->ShutdownPipeline(&Pipeline(st)); });
    if (im->live.empty()) {
        im->sock.Close();
        return fail("No usable source — stopping.");
    }

    for (deskhub::SourcePipelineState* st : im->live) im->AttachSession(&Pipeline(*st));

    {
        im->localInputMon.Start();
        const bool ax = macperm::HasAccessibility();
        LOGI("[Agent] Client control allowed (mouse + keyboard). Accessibility: %s%s",
            ax ? "YES" : "NO",
            ax ? "" : " — input will be silently dropped until it is granted");
    }
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

    for (auto& up : im->pipes) im->ShutdownPipeline(up.get());
    deskhubp::LogTransferTotals(im->AllStates());
    im->sock.Close();
    impl_.reset();
}

void AgentLoop::Impl::RecvLoop() {
    deskhubp::HostNetLoopHooks loop;
    loop.fallbackFps = opt.fps;
    loop.stopped = [this] { return quit.load(); };
    loop.onPeerDatagram = [this](const NetAddr& from) { replyAddr = from; };
    loop.publishStatus = [this] { PublishStatus(); };
    loop.source.closed = [](const deskhub::SourcePipelineState& st) {
        return Pipeline(st).capture.Closed();
    };
    loop.source.shutdown = [this](deskhub::SourcePipelineState& st) {
        ShutdownPipeline(&Pipeline(st));
    };
    loop.source.inputSkipped = [](const deskhub::SourcePipelineState& st) {
        return Pipeline(st).injector.skipped();
    };
    loop.source.takeIdleFrames = [](deskhub::SourcePipelineState& st) {
        return Pipeline(st).capture.TakeIdleCount();
    };
    loop.source.flush = [](deskhub::SourcePipelineState& st, uint64_t nowUs) {
        SourcePipeline& p = Pipeline(st);
        if (!p.haveCached.load(std::memory_order_acquire)) return;
        std::lock_guard<std::mutex> lk(p.encMutex);
        if (!p.cachedPb || !p.ensureEncoderFn(p.srcW.load(), p.srcH.load())) return;
        p.DiagEncode(p.encoder.get(), p.cachedPb, nowUs, p.forceIdr.exchange(false));
        p.encoder->Flush();
        p.lastKeepaliveUs = nowUs;
    };

    deskhubp::RunHostNetLoop(sock, beacon, live, loop);
}
