#include "AgentLoop.h"

#include <CoreVideo/CVPixelBuffer.h>

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <functional>
#include <utility>

#include "deskhubp/diag/Log.h"
#include "input/InputInjector.h"
#include "deskhubp/input/LocalInput.h"
#include "Permissions.h"
#include "capture/ScreenCapture.h"
#include "encode/VtEncoder.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/diag/LogFile.h"
#include "deskhubp/net/NetInfo.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/session/HostNetLoop.h"

#include "deskhub/control/BitrateController.h"
#include "deskhub/control/QualityLadder.h"
#include "deskhub/diag/AgentDiag.h"
#include "deskhub/session/Beacon.h"
#include "deskhub/session/HostRouter.h"
#include "deskhub/session/HostSession.h"
#include "deskhub/session/SourcePipelineState.h"
#include "deskhub/transport/Packetizer.h"
#include "deskhub/transport/RetransmitCache.h"

namespace {

struct SourcePipeline : deskhub::SourcePipelineState {
    SourcePipeline(uint32_t startBps, uint32_t minBps)
        : deskhub::SourcePipelineState(startBps, minBps, deskhub::diag::AgentDiagCaps{true, false}) {}

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
        if (!ok)
            LOGW("[DIAG][%s] evt=enc_fail idr=%d ms=%u", name.c_str(), idr ? 1 : 0, ms);
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
    void StartPipeline(SourcePipeline* p);
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
    std::vector<AgentSourceStatus> rows;
    std::vector<deskhub::SourceInfo> infos;
    for (SourcePipeline* p : live) {
        if (p->failed.load() || p->capture.Closed()) continue;

        deskhub::StatusExtras extras;
        if (const uint64_t peer = p->peerPacked.load(std::memory_order_relaxed))
            extras.viewerAddr = NetAddr::Unpack(peer).ToString();

        rows.push_back(deskhub::MakeSourceStatus(*p, extras));
        infos.push_back(deskhub::MakeSourceInfo(*p));
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

    if (sources.empty()) {
        LOGE("[Agent] No display to share.");
        return false;
    }
    if (sources.size() > deskhub::kMaxSources) {
        LOGE("[Agent] At most %zu sources can be shared at once.", deskhub::kMaxSources);
        return false;
    }
    if (!macperm::HasScreenRecording()) {
        LOGW(
            "[Agent] Screen Recording permission not detected — "
            "capture will likely fail. Grant it in System Settings and restart.");
    }

    if (!im->sock.Open(kDeskhubPort)) {
        LOGE(
            "[Agent] UDP port %u is not available — another Deskhub is probably "
            "still running. Close it and try again.",
            unsigned(kDeskhubPort));
        return false;
    }
    im->sock.SetRecvTimeout(100);

    im->startBitrate = opt.bitrateMbps * 1'000'000u;
    im->minBitrate = 1'000'000u;

    LOGI("[Agent] Listening on UDP port %u. On the other machine, enter one of:",
        unsigned(kDeskhubPort));
    for (const auto& a : ListLocalIPv4()) LOGI("    %s    (%s)", a.ip.c_str(), a.name.c_str());

    for (const AgentSource& s : sources) im->StartPipeline(im->MakePipeline(s));

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
        LOGE("[Agent] No usable source — stopping.");
        im->sock.Close();
        return false;
    }

    for (SourcePipeline* p : im->live) im->AttachSession(p);

    {
        im->localInputMon.Start();
        const bool ax = macperm::HasAccessibility();
        LOGI("[Agent] Client control allowed (mouse + keyboard). Accessibility: %s%s",
            ax ? "YES" : "NO",
            ax ? "" : " — input will be silently dropped until it is granted");
    }
    LOGI("[Agent] Sharing %zu source(s). Waiting for client...", im->live.size());

    im->PublishStatus();
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
    const std::vector<deskhub::SourcePipelineState*> liveStates(live.begin(), live.end());

    deskhubp::HostNetLoopHooks loop;
    loop.fallbackFps = opt.fps;
    loop.stopped = [this] { return quit.load(); };
    loop.onPeerDatagram = [this](const NetAddr& from) { replyAddr = from; };
    loop.publishStatus = [this] { PublishStatus(); };
    loop.source.closed = [](const deskhub::SourcePipelineState& st) {
        return static_cast<const SourcePipeline&>(st).capture.Closed();
    };
    loop.source.shutdown = [this](deskhub::SourcePipelineState& st) {
        ShutdownPipeline(static_cast<SourcePipeline*>(&st));
    };
    loop.source.inputSkipped = [](const deskhub::SourcePipelineState& st) {
        return static_cast<const SourcePipeline&>(st).injector.skipped();
    };
    loop.source.takeIdleFrames = [](deskhub::SourcePipelineState& st) {
        return static_cast<SourcePipeline&>(st).capture.TakeIdleCount();
    };
    loop.source.flush = [](deskhub::SourcePipelineState& st, uint64_t nowUs) {
        auto& p = static_cast<SourcePipeline&>(st);
        if (!p.haveCached.load(std::memory_order_acquire)) return;
        std::lock_guard<std::mutex> lk(p.encMutex);
        if (!p.cachedPb || !p.ensureEncoderFn(p.srcW.load(), p.srcH.load())) return;
        p.DiagEncode(p.encoder.get(), p.cachedPb, nowUs, p.forceIdr.exchange(false));
        p.encoder->Flush();
        p.lastKeepaliveUs = nowUs;
    };

    deskhubp::RunHostNetLoop(sock, beacon, liveStates, loop);
}
