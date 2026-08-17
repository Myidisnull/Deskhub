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
#include "gpu/GpuSelect.h"
#include "capture/ScreenCapture.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/diag/StallLog.h"
#include "deskhubp/input/LocalInput.h"
#include "deskhubp/system/Clock.h"
#include "encode/IVideoEncoder.h"
#include "input/InputInjector.h"
#include "net/Firewall.h"

#include "deskhub/control/FrameGate.h"
#include "deskhub/ui/Brand.h"
#include "deskhub/control/StreamSize.h"
#include "deskhub/diag/AgentDiag.h"
#include "deskhub/session/HostRouter.h"

namespace {

using WinSourceBase = deskhubp::HostSourceBase<ScreenCapture, InputInjector, IVideoEncoder>;

struct SourcePipeline : WinSourceBase {
    SourcePipeline(uint32_t startBps, uint32_t minBps)
        : WinSourceBase(startBps, minBps, deskhub::diag::AgentDiagCaps{}) {}

    HMONITOR monitor = nullptr;
    GpuChoice gpu;

    std::atomic<uint32_t> srcTexW{0}, srcTexH{0};
    std::atomic<uint32_t> frameCbDepth{0};
    std::atomic<uint32_t> encHoldDepth{0};
    std::atomic<uint32_t> encodeDepth{0};
    deskhub::FrameGate frameGate;

    Downscaler scaler;

    std::function<bool(uint32_t, uint32_t, uint32_t, uint32_t)> ensureEncoderFn;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> cachedTex;

    void ReleaseCached() {
        cachedTex.Reset();
        SetCachedFrame(false);
    }

    void LogStopSnapshot(const char* where, uint32_t waitedMs = 0) const {
        LOGI(
            "[DIAG][agent] evt=stop_snap where=%s waited_ms=%u name=%s frame_cb=%u enc_hold=%u "
            "encode=%u failed=%d net=%d has_encoder=%d tid=%lu",
            where, waitedMs, name.c_str(), frameCbDepth.load(std::memory_order_relaxed),
            encHoldDepth.load(std::memory_order_relaxed),
            encodeDepth.load(std::memory_order_relaxed),
            failed.load(std::memory_order_relaxed) ? 1 : 0,
            netReady.load(std::memory_order_relaxed) ? 1 : 0, encoder ? 1 : 0,
            (unsigned long)GetCurrentThreadId());
    }

    void EncodeTimed(ID3D11Texture2D* tex, bool idr) {
        encodeDepth.fetch_add(1, std::memory_order_acq_rel);
        const bool ok = deskhubp::DiagEncode(*this, idr,
            [this, tex, idr] { return encoder->Encode(tex, NowUs(), idr); });
        encodeDepth.fetch_sub(1, std::memory_order_acq_rel);
        if (ok) return;
        encoder.reset();
        forceIdr.store(true);
    }
};

SourcePipeline& Pipeline(deskhubp::HostSource& st) {
    return static_cast<SourcePipeline&>(st);
}

}

bool AgentLoop::Start(const std::vector<AgentSource>& sources, const AgentOptions& opt) {
    deskhubp::HostEngine* engine = &engine_;
    auto gpu = std::make_shared<GpuChoice>();

    deskhubp::HostEnginePolicy policy;
    policy.source = deskhubp::MakeDefaultSourcePolicy<SourcePipeline>();
    policy.status = deskhubp::MakeDefaultStatusHooks<SourcePipeline>();

    policy.preflight = [gpu] {
        if (!CreateBestDevice({GpuVendor::Nvidia, GpuVendor::Intel, GpuVendor::Amd}, *gpu))
            return std::string("Failed to create a D3D11 device.");
        Microsoft::WRL::ComPtr<ID3D10Multithread> mt;
        if (SUCCEEDED(gpu->device.As(&mt))) mt->SetMultithreadProtected(TRUE);
        LOGI("[Agent] GPU: %ls [%s]", gpu->description.c_str(), GpuVendorName(gpu->vendor));
        return std::string();
    };

    policy.afterSocket = [] {
        if (EnsureHostFirewallRule())
            LOGI("[Agent] Windows Firewall: inbound rule verified (all profiles).");
        else
            LOGW(
                "[Agent] Could not add/verify a Windows Firewall rule (needs admin). "
                "If the other machine cannot connect, allow %s through Windows "
                "Firewall for the current network.",
                deskhub::brand::kWindowsServiceName);
        return std::string();
    };

    policy.onSharing = [] {
        const bool elevated = IsProcessElevated();
        LOGI("[Agent] Client control allowed (mouse + keyboard). Host elevated: %s%s",
            elevated ? "YES" : "NO",
            elevated ? "" : " \xE2\x80\x94 input will NOT reach apps running as administrator");
    };

    policy.source.create = [engine](const AgentSource& s,
                               uint8_t sourceId) -> std::unique_ptr<deskhubp::HostSource> {
        auto p = deskhubp::MakeHostSource<SourcePipeline>(*engine, s, sourceId);
        p->monitor = (HMONITOR)(uintptr_t)s.targetId;
        return p;
    };

    policy.source.startCapture = [engine](deskhubp::HostSource& st) {
        SourcePipeline* p = &Pipeline(st);
        const uint32_t fps = engine->options().fps;
        const uint32_t maxDim = engine->options().maxDim;

        if (!CreateBestDevice({GpuVendor::Nvidia, GpuVendor::Intel, GpuVendor::Amd}, p->gpu)) {
            LOGE("[Agent][%s] Failed to create a D3D11 device for this source.",
                p->name.c_str());
            p->failed.store(true);
            return;
        }
        {
            Microsoft::WRL::ComPtr<ID3D10Multithread> mt;
            if (SUCCEEDED(p->gpu.device.As(&mt))) mt->SetMultithreadProtected(TRUE);
        }

        auto onPacket = engine->MakePacketSink(*p);

        p->ensureEncoderFn = [p, fps, onPacket](uint32_t w, uint32_t h, uint32_t sw,
                                 uint32_t sh) -> bool {
            if (p->encoder && p->encoder->IsOpen()) return true;
            EncoderConfig cfg;
            static_cast<deskhub::media::EncoderConfig&>(cfg) =
                deskhub::MakeEncoderConfig(*p, {w, h}, fps);
            cfg.srcWidth = sw;
            cfg.srcHeight = sh;
            cfg.onPacket = onPacket;
            p->encoder = CreateEncoder(p->gpu.device.Get(), cfg);
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

        auto onFrame = [p, maxDim](const FrameInfo& fi) {
            p->frameCbDepth.fetch_add(1, std::memory_order_acq_rel);
            struct DepthGuard {
                std::atomic<uint32_t>& depth;
                ~DepthGuard() {
                    depth.fetch_sub(1, std::memory_order_acq_rel);
                }
            } frameGuard{p->frameCbDepth};

            p->captured.fetch_add(1, std::memory_order_relaxed);
            if (p->failed.load()) return;

            p->encHoldDepth.fetch_add(1, std::memory_order_acq_rel);
            std::lock_guard<std::mutex> lk(p->encMutex);
            struct HoldGuard {
                std::atomic<uint32_t>& depth;
                ~HoldGuard() {
                    depth.fetch_sub(1, std::memory_order_acq_rel);
                }
            } holdGuard{p->encHoldDepth};

            const deskhub::FrameAdmission adm = deskhub::AdmitCapturedFrame(*p, fi.meta.width,
                fi.meta.height, maxDim);
            if (adm.rebuildEncoder) {
                p->encoder.reset();
                p->ReleaseCached();
            }
            if (!adm.sizeNote.empty())
                LOGI("[Agent][%s] %s", p->name.c_str(), adm.sizeNote.c_str());
            if (!adm.pauseNote.empty())
                LOGI("[Agent][%s] %s", p->name.c_str(), adm.pauseNote.c_str());
            if (adm.drop) return;
            const uint32_t encW = adm.encode.width, encH = adm.encode.height;

            const uint64_t frameUs = NowUs();
            if (!p->frameGate.Admit(p->curFps.load(std::memory_order_relaxed), frameUs)) return;

            ID3D11Texture2D* encTex = fi.handle;
            if (encW != fi.meta.width || encH != fi.meta.height) {
                if (!p->scaler.Configure(p->gpu.device.Get(), fi.meta.width, fi.meta.height, encW,
                        encH))
                    return;
                encTex = p->scaler.Scale(fi.handle);
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
                if (FAILED(p->gpu.device->CreateTexture2D(&d, nullptr,
                        p->cachedTex.GetAddressOf())))
                    p->cachedTex.Reset();
            }
            if (p->cachedTex) {
                p->gpu.context->CopyResource(p->cachedTex.Get(), encTex);
                p->SetCachedFrame(true);
            }
            p->lastFrameUs.store(frameUs, std::memory_order_relaxed);

            if (!p->netReady.load(std::memory_order_acquire)) return;
            if (!p->ensureEncoderFn(encW, encH, encW, encH)) return;
            p->EncodeTimed(encTex, p->forceIdr.exchange(false));
        };

        p->capture.SetDevice(p->gpu.device.Get());
        if (!p->capture.Start(uint64_t(uintptr_t(p->monitor)),
                deskhub::media::CaptureOptions{fps, maxDim}, onFrame)) {
            LOGE("[Agent][%s] Failed to start capture \xE2\x80\x94 skipping this source.",
                p->name.c_str());
            p->failed.store(true);
        }
    };

    policy.source.stopCapture = [](deskhubp::HostSource& st) {
        SourcePipeline& p = Pipeline(st);
        p.LogStopSnapshot("stop_capture_enter");
        {
            const uint64_t t0 = NowUs();
            deskhubp::StopAnrWatch watch("agent", "capture_stop",
                [&p](uint32_t ms) { p.LogStopSnapshot("capture_stop_blocked", ms); });
            p.capture.Stop();
            deskhubp::LogStopPhase("agent", "capture_stop", t0);
        }
        p.LogStopSnapshot("after_capture_stop");
        std::unique_lock<std::mutex> lk(p.encMutex, std::defer_lock);
        {
            const uint64_t t0 = NowUs();
            deskhubp::StopAnrWatch watch("agent", "encoder_lock",
                [&p](uint32_t ms) { p.LogStopSnapshot("encoder_lock_blocked", ms); });
            if (!deskhubp::TimedTryLock(lk, 3000)) {
                LOGE(
                    "[DIAG][agent] evt=stop_anr phase=encoder_lock ms=%u "
                    "state=timeout_force_continue",
                    deskhubp::ElapsedMsSince(t0));
                p.LogStopSnapshot("encoder_lock_timeout", deskhubp::ElapsedMsSince(t0));
                while (!lk.try_lock()) SleepUs(1000);
            }
            deskhubp::LogStopPhase("agent", "encoder_lock", t0);
        }
        p.LogStopSnapshot("encoder_lock_held");
        {
            const uint64_t t0 = NowUs();
            LOGI("[DIAG][agent] evt=encoder_drop_begin has_encoder=%d policy=skip_eos",
                p.encoder ? 1 : 0);
            deskhubp::StopAnrWatch watch("agent", "encoder_drop",
                [&p](uint32_t ms) { p.LogStopSnapshot("encoder_drop_blocked", ms); });
            p.encoder.reset();
            p.ReleaseCached();
            deskhubp::LogStopPhase("agent", "encoder_drop", t0);
        }
        p.LogStopSnapshot("stop_capture_done");
    };

    policy.source.attachInput = [engine](deskhubp::HostSource& st) {
        SourcePipeline& p = Pipeline(st);
        p.injector.SetLocalMonitor(&engine->localInput());
        p.injector.SetEnabled(p.injector.Init(p.monitor));
    };

    policy.source.retarget = [engine](deskhubp::HostSource& st) {
        return deskhub::RetargetStream(st, engine->options().maxDim);
    };

    policy.source.applyQualityStep = [engine](deskhubp::HostSource& st,
                                         const deskhub::QualityStep& prev,
                                         const deskhub::QualityStep& next) {
        SourcePipeline& p = Pipeline(st);
        const deskhub::StreamSize t = deskhub::RetargetStream(st, engine->options().maxDim);
        auto lk = deskhubp::TryHoldEncoder(p.encMutex);
        if (lk.owns_lock() && p.encoder && prev.fps != next.fps) p.encoder->SetFps(next.fps);
        return t;
    };

    policy.source.flush = [](deskhubp::HostSource& st, uint64_t) {
        SourcePipeline& p = Pipeline(st);
        if (!p.hasCachedFrame()) return;
        auto lk = deskhubp::TryHoldEncoder(p.encMutex);
        if (!lk.owns_lock()) return;
        if (!p.ensureEncoderFn(p.srcW.load(), p.srcH.load(), p.srcTexW.load(),
                p.srcTexH.load()))
            return;
        p.EncodeTimed(p.cachedTex.Get(), p.forceIdr.exchange(false));
    };

    return engine_.Start(sources, opt, std::move(policy));
}
