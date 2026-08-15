#include "deskhubp/session/AgentLoop.h"

#include <CoreVideo/CVPixelBuffer.h>

#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <utility>

#include "Permissions.h"
#include "capture/ScreenCapture.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/input/LocalInput.h"
#include "deskhubp/media/VtEncoder.h"
#include "deskhubp/system/Clock.h"
#include "input/InputInjector.h"

#include "deskhub/control/StreamSize.h"
#include "deskhub/diag/AgentDiag.h"
#include "deskhub/session/HostRouter.h"

namespace {

using MacSourceBase = deskhubp::HostSourceBase<ScreenCapture, InputInjector, VtEncoder>;

struct SourcePipeline : MacSourceBase {
    SourcePipeline(uint32_t startBps, uint32_t minBps)
        : MacSourceBase(startBps, minBps, deskhub::diag::AgentDiagCaps{true, false}) {}

    ~SourcePipeline() override {
        ReleaseCached();
    }

    uint32_t displayId = 0;

    std::function<bool(uint32_t, uint32_t)> ensureEncoderFn;

    void* cachedPb = nullptr;

    void ReleaseCached() {
        if (!cachedPb) return;
        CVPixelBufferRelease(static_cast<CVPixelBufferRef>(cachedPb));
        cachedPb = nullptr;
        SetCachedFrame(false);
    }

    void EncodeTimed(void* pb, uint64_t tsUs, bool idr) {
        const bool ok = deskhubp::DiagEncode(*this, idr,
            [this, pb, tsUs, idr] { return encoder->Encode(pb, tsUs, idr); });
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

    deskhubp::HostEnginePolicy policy;
    policy.source = deskhubp::MakeDefaultSourcePolicy<SourcePipeline>();
    policy.status = deskhubp::MakeDefaultStatusHooks<SourcePipeline>();
    policy.noSourceError = "No display to share.";
    policy.onApprovalNeeded = [this](uint64_t addrPacked, std::string shortKey,
                                  std::string name) {
        PushPairingRequest(PairingRequest{addrPacked, std::move(shortKey), std::move(name)});
    };

    policy.preflight = [] {
        if (!macperm::HasScreenRecording())
            LOGW(
                "[Agent] Screen Recording permission not detected \xE2\x80\x94 "
                "capture will likely fail. Grant it in System Settings and restart.");
        return std::string();
    };

    policy.onSharing = [] {
        const bool ax = macperm::HasAccessibility();
        LOGI("[Agent] Client control allowed (mouse + keyboard). Accessibility: %s%s",
            ax ? "YES" : "NO",
            ax ? "" : " \xE2\x80\x94 input will be silently dropped until it is granted");
    };

    policy.source.create = [engine](const AgentSource& s,
                               uint8_t sourceId) -> std::unique_ptr<deskhubp::HostSource> {
        auto p = deskhubp::MakeHostSource<SourcePipeline>(*engine, s, sourceId);
        p->displayId = uint32_t(s.targetId);
        return p;
    };

    policy.source.startCapture = [engine](deskhubp::HostSource& st) {
        SourcePipeline* p = &Pipeline(st);
        const uint32_t fps = engine->options().fps;
        const uint32_t maxDim = engine->options().maxDim;
        p->curFps.store(fps, std::memory_order_relaxed);

        auto onPacket = engine->MakePacketSink(*p);

        p->ensureEncoderFn = [p, fps, onPacket](uint32_t w, uint32_t h) -> bool {
            if (p->encoder && p->encoder->IsOpen()) return true;
            EncoderConfig cfg = deskhub::MakeEncoderConfig(*p, {w, h}, fps);
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

        auto onFrame = [p, maxDim](const MacFrameInfo& fi) {
            p->captured.fetch_add(1, std::memory_order_relaxed);
            if (p->failed.load()) return;

            std::lock_guard<std::mutex> lk(p->encMutex);

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

            if (p->cachedPb) CVPixelBufferRelease(static_cast<CVPixelBufferRef>(p->cachedPb));
            p->cachedPb = CVPixelBufferRetain(static_cast<CVPixelBufferRef>(fi.handle));
            p->SetCachedFrame(p->cachedPb != nullptr);
            p->lastFrameUs.store(fi.meta.timestampUs, std::memory_order_relaxed);

            if (!p->netReady.load(std::memory_order_acquire)) return;
            if (!p->ensureEncoderFn(adm.encode.width, adm.encode.height)) return;
            p->EncodeTimed(fi.handle, fi.meta.timestampUs, p->forceIdr.exchange(false));
        };

        if (!p->capture.Start(p->displayId,
                deskhub::media::CaptureOptions{fps, engine->options().maxDim}, onFrame)) {
            LOGE("[Agent][%s] Failed to start capture \xE2\x80\x94 skipping this source.",
                p->name.c_str());
            p->failed.store(true);
        }
    };

    policy.source.stopCapture = [](deskhubp::HostSource& st) {
        SourcePipeline& p = Pipeline(st);
        p.capture.Stop();
        std::lock_guard<std::mutex> lk(p.encMutex);
        if (p.encoder) p.encoder->Finish();
        p.ReleaseCached();
    };

    policy.source.attachInput = [engine](deskhubp::HostSource& st) {
        SourcePipeline& p = Pipeline(st);
        p.injector.SetLocalMonitor(&engine->localInput());
        p.injector.SetEnabled(p.injector.Init(p.displayId));
    };

    policy.source.retarget = [](deskhubp::HostSource& st) {
        SourcePipeline& p = Pipeline(st);
        uint32_t w = 0, h = 0;
        p.capture.SetClientSize(uint16_t(p.cliW), uint16_t(p.cliH), w, h);
        return deskhub::StreamSize{w, h};
    };

    policy.source.applyQualityStep = [](deskhubp::HostSource& st, const deskhub::QualityStep&,
                                         const deskhub::QualityStep& next) {
        SourcePipeline& p = Pipeline(st);
        {
            std::lock_guard<std::mutex> lk(p.encMutex);
            if (p.encoder) p.encoder->SetFps(next.fps);
        }
        uint32_t w = 0, h = 0;
        p.capture.SetQuality(next.scalePct, next.fps, w, h);
        return deskhub::StreamSize{w, h};
    };

    policy.source.takeIdleFrames = [](deskhubp::HostSource& st) {
        return Pipeline(st).capture.TakeIdleCount();
    };

    policy.source.flush = [](deskhubp::HostSource& st, uint64_t nowUs) {
        SourcePipeline& p = Pipeline(st);
        if (!p.hasCachedFrame()) return;
        auto lk = deskhubp::TryHoldEncoder(p.encMutex);
        if (!lk.owns_lock()) return;
        if (!p.cachedPb || !p.ensureEncoderFn(p.srcW.load(), p.srcH.load())) return;
        p.EncodeTimed(p.cachedPb, nowUs, p.forceIdr.exchange(false));
        if (p.encoder) p.encoder->Flush();
    };

    return engine_.Start(sources, opt, std::move(policy));
}
