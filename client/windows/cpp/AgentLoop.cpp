#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include "AgentLoop.h"

#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "ElevatedShare.h"
#include "capture/Downscaler.h"
#include "capture/GpuSelect.h"
#include "capture/ScreenCapture.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/diag/LogFile.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/session/HostAgent.h"
#include "deskhubp/session/HostNetLoop.h"
#include "deskhubp/system/Clock.h"
#include "encode/IVideoEncoder.h"
#include "input/InputInjector.h"
#include "deskhubp/input/LocalInput.h"
#include "net/Firewall.h"

#include "deskhub/control/FrameGate.h"
#include "deskhub/control/QualityLadder.h"
#include "deskhub/control/StreamSize.h"
#include "deskhub/diag/AgentDiag.h"
#include "deskhub/session/Beacon.h"
#include "deskhub/session/HostRouter.h"
#include "deskhub/session/HostSession.h"
#include "deskhub/session/SourcePipelineState.h"

namespace {

struct SourcePipeline : deskhub::SourcePipelineState {
    SourcePipeline(uint32_t startBps, uint32_t minBps)
        : deskhub::SourcePipelineState(startBps, minBps, deskhub::diag::AgentDiagCaps{}) {}

    HMONITOR monitor = nullptr;

    ScreenCapture capture;
    InputInjector injector;

    std::atomic<uint32_t> srcTexW{0}, srcTexH{0};
    deskhub::FrameGate frameGate;

    Downscaler scaler;

    std::mutex encMutex;
    std::unique_ptr<IVideoEncoder> encoder;
    std::function<bool(uint32_t, uint32_t, uint32_t, uint32_t)> ensureEncoderFn;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> cachedTex;
    std::atomic<bool> haveCached{false};

    void DiagEncode(IVideoEncoder* enc, ID3D11Texture2D* tex, bool idr) {
        const uint64_t t0 = NowUs();
        const bool ok = enc->Encode(tex, t0, idr);
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

    GpuChoice gpu;

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
    p->monitor = (HMONITOR)(uintptr_t)s.targetId;
    p->name = s.name;
    pipes.push_back(std::move(p));
    return pipes.back().get();
}

void AgentLoop::Impl::StartPipeline(SourcePipeline* p) {
    UdpSocket* sockPtr = &sock;
    GpuChoice* gpuPtr = &gpu;

    auto onPacket = [p, sockPtr](const uint8_t* data, size_t size, uint64_t tsUs,
                        bool keyframe) {
        deskhubp::SendEncodedFrame(*p, *sockPtr, std::span<const uint8_t>(data, size), tsUs,
            keyframe);
    };

    const uint32_t fps = opt.fps;
    auto ensureEncoder = [p, gpuPtr, fps, onPacket](uint32_t w, uint32_t h, uint32_t sw,
                             uint32_t sh) -> bool {
        if (p->encoder) return true;
        EncoderConfig cfg;
        cfg.width = w;
        cfg.height = h;
        cfg.srcWidth = sw;
        cfg.srcHeight = sh;
        cfg.fps = p->curFps.load(std::memory_order_relaxed);
        if (!cfg.fps) cfg.fps = fps;
        cfg.bitrateBps = p->curBitrateBps.load(std::memory_order_relaxed);
        cfg.outputPath.clear();
        cfg.onPacket = onPacket;
        p->encoder = CreateEncoder(gpuPtr->device.Get(), cfg);
        if (!p->encoder) {
            LOGE(
                "[Agent][%s] No usable encoder backend (NVENC + Media Foundation"
                " both failed).",
                p->name.c_str());
            p->failed.store(true);
            return false;
        }
        return true;
    };
    p->ensureEncoderFn = ensureEncoder;

    const uint32_t maxDim = opt.maxDim;
    auto onFrame = [p, gpuPtr, ensureEncoder, maxDim](const FrameInfo& fi) {
        p->captured.fetch_add(1, std::memory_order_relaxed);
        if (p->failed.load()) return;
        if (!fi.meta.width || !fi.meta.height) return;

        p->nativeW.store(fi.meta.width, std::memory_order_relaxed);
        p->nativeH.store(fi.meta.height, std::memory_order_relaxed);

        const deskhub::EncodeSize target = deskhub::ClampEncodeSize(fi.meta.width,
            fi.meta.height, p->wantW.load(std::memory_order_relaxed),
            p->wantH.load(std::memory_order_relaxed), maxDim);
        const uint32_t encW = target.width(), encH = target.height();
        if (!encW || !encH) return;

        std::lock_guard<std::mutex> lk(p->encMutex);

        if (p->srcW.load() != encW || p->srcH.load() != encH) {
            if (p->srcW.load())
                LOGI(
                    "[Agent][%s] Encode size %ux%u -> %ux%u (source %ux%u),"
                    " rebuilding encoder.",
                    p->name.c_str(), p->srcW.load(), p->srcH.load(), encW, encH,
                    fi.meta.width, fi.meta.height);
            p->srcW.store(encW);
            p->srcH.store(encH);
            p->encoder.reset();
            p->cachedTex.Reset();
            p->haveCached.store(false, std::memory_order_release);
            p->sizeChanged.store(true, std::memory_order_release);
        }

        if (target.tooSmall) {
            if (!p->paused.exchange(true, std::memory_order_acq_rel))
                LOGI(
                    "[Agent][%s] Source too small to encode (%ux%u) —"
                    " paused, waiting for it to grow back.",
                    p->name.c_str(), encW, encH);
            return;
        }
        if (p->paused.exchange(false, std::memory_order_acq_rel))
            LOGI("[Agent][%s] Source back to %ux%u — resuming.",
                p->name.c_str(), encW, encH);

        const uint64_t frameUs = NowUs();
        if (!p->frameGate.Admit(p->curFps.load(std::memory_order_relaxed), frameUs)) return;

        ID3D11Texture2D* encTex = fi.texture;
        if (encW != fi.meta.width || encH != fi.meta.height) {
            if (!p->scaler.Configure(gpuPtr->device.Get(), fi.meta.width, fi.meta.height, encW,
                    encH))
                return;
            encTex = p->scaler.Scale(fi.texture);
            if (!encTex) return;
        }
        p->srcTexW.store(encW);
        p->srcTexH.store(encH);

        if (!p->cachedTex) {
            D3D11_TEXTURE2D_DESC d{};
            encTex->GetDesc(&d);
            d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            d.MiscFlags = 0;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.CPUAccessFlags = 0;
            if (FAILED(gpuPtr->device->CreateTexture2D(&d, nullptr,
                    p->cachedTex.GetAddressOf())))
                p->cachedTex.Reset();
        }
        if (p->cachedTex) {
            gpuPtr->context->CopyResource(p->cachedTex.Get(), encTex);
            p->haveCached.store(true, std::memory_order_release);
        }
        p->lastFrameUs.store(frameUs, std::memory_order_relaxed);

        if (!p->netReady.load(std::memory_order_acquire)) return;
        if (!ensureEncoder(encW, encH, encW, encH)) return;
        p->DiagEncode(p->encoder.get(), encTex, p->forceIdr.exchange(false));
    };

    p->capture.SetDevice(gpu.device.Get());
    if (!p->capture.Start(uint64_t(uintptr_t(p->monitor)),
            deskhub::media::CaptureOptions{opt.fps, opt.maxDim}, onFrame)) {
        LOGW("[Agent][%s] Failed to start capture — skipping this source.", p->name.c_str());
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
    p->injector.SetEnabled(p->injector.Init(p->monitor));

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
    hooks.retarget = [p, self] { return deskhub::RetargetStream(*p, self->opt.maxDim); };
    hooks.applyInput = [p](const deskhub::InputEvent& e) { p->injector.Apply(e); };
    hooks.releaseInput = [p] { p->injector.ReleaseAll(); };
    hooks.setEncoderBitrate = [p](uint32_t bitrateBps) {
        std::lock_guard<std::mutex> lk(p->encMutex);
        return p->encoder && p->encoder->SetBitrate(bitrateBps);
    };
    hooks.applyQualityStep = [p, self](const deskhub::QualityStep& prev,
                                 const deskhub::QualityStep& next) {
        const deskhub::StreamSize t = deskhub::RetargetStream(*p, self->opt.maxDim);
        std::lock_guard<std::mutex> lk(p->encMutex);
        if (p->encoder && prev.fps != next.fps) p->encoder->SetFps(next.fps);
        return t;
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

    if (sources.empty()) return fail("No source selected.");
    if (sources.size() > deskhub::kMaxSources)
        return fail("At most " + std::to_string(deskhub::kMaxSources) +
                    " sources can be shared at once.");

    if (!CreateBestDevice({GpuVendor::Nvidia, GpuVendor::Intel, GpuVendor::Amd}, im->gpu))
        return fail("Failed to create a D3D11 device.");
    {
        Microsoft::WRL::ComPtr<ID3D10Multithread> mt;
        if (SUCCEEDED(im->gpu.device.As(&mt))) mt->SetMultithreadProtected(TRUE);
    }
    LOGI("[Agent] GPU: %ls [%ls]", im->gpu.description.c_str(),
        GpuVendorName(im->gpu.vendor));

    if (!im->sock.Open(kDeskhubPort))
        return fail(im->sock.lastBindAddrInUse()
                        ? "UDP port " + std::to_string(kDeskhubPort) +
                              " is already in use — another Deskhub is probably still "
                              "running. Close it and try again."
                        : "Could not open UDP port " + std::to_string(kDeskhubPort) + ".");
    im->sock.SetRecvTimeout(100);

    im->startBitrate = opt.bitrateMbps * 1'000'000u;
    im->minBitrate = 1'000'000u;

    if (EnsureHostFirewallRule())
        LOGI("[Agent] Windows Firewall: inbound rule verified (all profiles).");
    else
        LOGW(
            "[Agent] Could not add/verify a Windows Firewall rule (needs admin). "
            "If the other machine cannot connect, allow Deskhub.exe through Windows "
            "Firewall for the current network.");

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

    im->localInputMon.Start();
    {
        const bool elevated = IsProcessElevated();
        LOGI("[Agent] Client control allowed (mouse + keyboard). Host elevated: %s%s",
            elevated ? "YES" : "NO",
            elevated ? "" : " — input will NOT reach apps running as administrator");
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
    loop.source.flush = [](deskhub::SourcePipelineState& st, uint64_t nowUs) {
        SourcePipeline& p = Pipeline(st);
        if (!p.haveCached.load(std::memory_order_acquire)) return;
        std::lock_guard<std::mutex> lk(p.encMutex);
        if (!p.ensureEncoderFn(p.srcW.load(), p.srcH.load(), p.srcTexW.load(),
                p.srcTexH.load()))
            return;
        p.DiagEncode(p.encoder.get(), p.cachedTex.Get(), p.forceIdr.exchange(false));
        p.lastKeepaliveUs = nowUs;
    };

    deskhubp::RunHostNetLoop(sock, beacon, live, loop);
}
