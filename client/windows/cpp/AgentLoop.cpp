#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include "deskhubp/session/AgentLoop.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <utility>

#include "ElevatedShare.h"
#include "capture/Downscaler.h"
#include "capture/GpuSelect.h"
#include "capture/ScreenCapture.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/input/LocalInput.h"
#include "deskhubp/system/Clock.h"
#include "encode/IVideoEncoder.h"
#include "input/InputInjector.h"
#include "net/Firewall.h"

#include "deskhub/control/FrameGate.h"
#include "deskhub/control/StreamSize.h"
#include "deskhub/diag/AgentDiag.h"
#include "deskhub/session/HostRouter.h"

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

    void DiagEncode(ID3D11Texture2D* tex, bool idr) {
        const uint64_t t0 = NowUs();
        const bool ok = encoder->Encode(tex, t0, idr);
        const uint32_t ms = uint32_t((NowUs() - t0) / 1000);
        diag.encMs.Add(ms);
        if (!ok) LOGW("[DIAG][%s] evt=enc_fail idr=%d ms=%u", name.c_str(), idr ? 1 : 0, ms);
    }
};

SourcePipeline& Pipeline(deskhubp::HostSource& st) {
    return static_cast<SourcePipeline&>(st);
}

const SourcePipeline& Pipeline(const deskhubp::HostSource& st) {
    return static_cast<const SourcePipeline&>(st);
}

}

bool AgentLoop::Start(const std::vector<AgentSource>& sources, const AgentOptions& opt) {
    deskhubp::HostEngine* engine = &engine_;
    auto gpu = std::make_shared<GpuChoice>();

    deskhubp::HostEnginePolicy policy;

    policy.preflight = [gpu] {
        if (!CreateBestDevice({GpuVendor::Nvidia, GpuVendor::Intel, GpuVendor::Amd}, *gpu))
            return std::string("Failed to create a D3D11 device.");
        Microsoft::WRL::ComPtr<ID3D10Multithread> mt;
        if (SUCCEEDED(gpu->device.As(&mt))) mt->SetMultithreadProtected(TRUE);
        LOGI("[Agent] GPU: %ls [%ls]", gpu->description.c_str(), GpuVendorName(gpu->vendor));
        return std::string();
    };

    policy.portError = [](const UdpSocket& sock) {
        return sock.lastBindAddrInUse()
                   ? "UDP port " + std::to_string(kDeskhubPort) +
                         " is already in use \xE2\x80\x94 another Deskhub is probably still "
                         "running. Close it and try again."
                   : "Could not open UDP port " + std::to_string(kDeskhubPort) + ".";
    };

    policy.afterSocket = [] {
        if (EnsureHostFirewallRule())
            LOGI("[Agent] Windows Firewall: inbound rule verified (all profiles).");
        else
            LOGW(
                "[Agent] Could not add/verify a Windows Firewall rule (needs admin). "
                "If the other machine cannot connect, allow Deskhub.exe through Windows "
                "Firewall for the current network.");
        return std::string();
    };

    policy.onSharing = [] {
        const bool elevated = IsProcessElevated();
        LOGI("[Agent] Client control allowed (mouse + keyboard). Host elevated: %s%s",
            elevated ? "YES" : "NO",
            elevated ? "" : " \xE2\x80\x94 input will NOT reach apps running as administrator");
    };

    policy.status.closed = [](const deskhubp::HostSource& st) {
        return Pipeline(st).capture.Closed();
    };

    policy.source.create = [engine](const AgentSource& s,
                               uint8_t sourceId) -> std::unique_ptr<deskhubp::HostSource> {
        auto p = std::make_unique<SourcePipeline>(engine->startBitrateBps(),
            deskhubp::HostEngine::kMinBitrateBps);
        p->sourceId = sourceId;
        p->monitor = (HMONITOR)(uintptr_t)s.targetId;
        p->name = s.name;
        return p;
    };

    policy.source.startCapture = [engine, gpu](deskhubp::HostSource& st) {
        SourcePipeline* p = &Pipeline(st);
        const uint32_t fps = engine->options().fps;
        const uint32_t maxDim = engine->options().maxDim;

        auto onPacket = [p, engine](const uint8_t* data, size_t size, uint64_t tsUs,
                            bool keyframe) {
            deskhubp::SendEncodedFrame(*p, engine->socket(),
                std::span<const uint8_t>(data, size), tsUs, keyframe);
        };

        p->ensureEncoderFn = [p, gpu, fps, onPacket](uint32_t w, uint32_t h, uint32_t sw,
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
            p->encoder = CreateEncoder(gpu->device.Get(), cfg);
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

        auto onFrame = [p, gpu, maxDim](const FrameInfo& fi) {
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
                        "[Agent][%s] Source too small to encode (%ux%u) \xE2\x80\x94"
                        " paused, waiting for it to grow back.",
                        p->name.c_str(), encW, encH);
                return;
            }
            if (p->paused.exchange(false, std::memory_order_acq_rel))
                LOGI("[Agent][%s] Source back to %ux%u \xE2\x80\x94 resuming.", p->name.c_str(),
                    encW, encH);

            const uint64_t frameUs = NowUs();
            if (!p->frameGate.Admit(p->curFps.load(std::memory_order_relaxed), frameUs)) return;

            ID3D11Texture2D* encTex = fi.texture;
            if (encW != fi.meta.width || encH != fi.meta.height) {
                if (!p->scaler.Configure(gpu->device.Get(), fi.meta.width, fi.meta.height, encW,
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
                if (FAILED(gpu->device->CreateTexture2D(&d, nullptr,
                        p->cachedTex.GetAddressOf())))
                    p->cachedTex.Reset();
            }
            if (p->cachedTex) {
                gpu->context->CopyResource(p->cachedTex.Get(), encTex);
                p->haveCached.store(true, std::memory_order_release);
            }
            p->lastFrameUs.store(frameUs, std::memory_order_relaxed);

            if (!p->netReady.load(std::memory_order_acquire)) return;
            if (!p->ensureEncoderFn(encW, encH, encW, encH)) return;
            p->DiagEncode(encTex, p->forceIdr.exchange(false));
        };

        p->capture.SetDevice(gpu->device.Get());
        if (!p->capture.Start(uint64_t(uintptr_t(p->monitor)),
                deskhub::media::CaptureOptions{fps, maxDim}, onFrame)) {
            LOGW("[Agent][%s] Failed to start capture \xE2\x80\x94 skipping this source.",
                p->name.c_str());
            p->failed.store(true);
        }
    };

    policy.source.stopCapture = [](deskhubp::HostSource& st) {
        SourcePipeline& p = Pipeline(st);
        p.capture.Stop();
        std::lock_guard<std::mutex> lk(p.encMutex);
        if (p.encoder) p.encoder->Finish();
    };

    policy.source.attachInput = [engine](deskhubp::HostSource& st) {
        SourcePipeline& p = Pipeline(st);
        p.injector.SetLocalMonitor(&engine->localInput());
        p.injector.SetEnabled(p.injector.Init(p.monitor));
    };

    policy.source.releaseInput = [](deskhubp::HostSource& st) {
        Pipeline(st).injector.ReleaseAll();
    };

    policy.source.applyInput = [](deskhubp::HostSource& st, const deskhub::InputEvent& e) {
        Pipeline(st).injector.Apply(e);
    };

    policy.source.setEncoderBitrate = [](deskhubp::HostSource& st, uint32_t bitrateBps) {
        SourcePipeline& p = Pipeline(st);
        std::lock_guard<std::mutex> lk(p.encMutex);
        return p.encoder && p.encoder->SetBitrate(bitrateBps);
    };

    policy.source.retarget = [engine](deskhubp::HostSource& st) {
        return deskhub::RetargetStream(st, engine->options().maxDim);
    };

    policy.source.applyQualityStep = [engine](deskhubp::HostSource& st,
                                         const deskhub::QualityStep& prev,
                                         const deskhub::QualityStep& next) {
        SourcePipeline& p = Pipeline(st);
        const deskhub::StreamSize t = deskhub::RetargetStream(st, engine->options().maxDim);
        std::lock_guard<std::mutex> lk(p.encMutex);
        if (p.encoder && prev.fps != next.fps) p.encoder->SetFps(next.fps);
        return t;
    };

    policy.source.inputSkipped = [](const deskhubp::HostSource& st) {
        return Pipeline(st).injector.skipped();
    };

    policy.source.flush = [](deskhubp::HostSource& st, uint64_t nowUs) {
        SourcePipeline& p = Pipeline(st);
        if (!p.haveCached.load(std::memory_order_acquire)) return;
        std::lock_guard<std::mutex> lk(p.encMutex);
        if (!p.ensureEncoderFn(p.srcW.load(), p.srcH.load(), p.srcTexW.load(),
                p.srcTexH.load()))
            return;
        p.DiagEncode(p.cachedTex.Get(), p.forceIdr.exchange(false));
        p.lastKeepaliveUs = nowUs;
    };

    return engine_.Start(sources, opt, std::move(policy));
}
