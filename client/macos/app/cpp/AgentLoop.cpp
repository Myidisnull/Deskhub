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
#include "deskhubp/system/Clock.h"
#include "encode/VtEncoder.h"
#include "input/InputInjector.h"

#include "deskhub/control/StreamSize.h"
#include "deskhub/diag/AgentDiag.h"

namespace {

struct SourcePipeline : deskhub::SourcePipelineState {
    SourcePipeline(uint32_t startBps, uint32_t minBps)
        : deskhub::SourcePipelineState(startBps, minBps,
              deskhub::diag::AgentDiagCaps{true, false}) {}

    ~SourcePipeline() {
        ReleaseCached();
    }

    uint32_t displayId = 0;

    ScreenCapture capture;
    InputInjector injector;

    std::mutex encMutex;
    std::unique_ptr<VtEncoder> encoder;
    std::function<bool(uint32_t, uint32_t)> ensureEncoderFn;

    void* cachedPb = nullptr;
    std::atomic<bool> haveCached{false};

    void ReleaseCached() {
        if (!cachedPb) return;
        CVPixelBufferRelease(static_cast<CVPixelBufferRef>(cachedPb));
        cachedPb = nullptr;
        haveCached.store(false, std::memory_order_release);
    }

    void DiagEncode(void* pb, uint64_t tsUs, bool idr) {
        const uint64_t t0 = NowUs();
        const bool ok = encoder->Encode(pb, tsUs, idr);
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

    deskhubp::HostEnginePolicy policy;
    policy.noSourceError = "No display to share.";

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

    policy.status.closed = [](const deskhubp::HostSource& st) {
        return Pipeline(st).capture.Closed();
    };

    policy.source.create = [engine](const AgentSource& s,
                               uint8_t sourceId) -> std::unique_ptr<deskhubp::HostSource> {
        auto p = std::make_unique<SourcePipeline>(engine->startBitrateBps(),
            deskhubp::HostEngine::kMinBitrateBps);
        p->sourceId = sourceId;
        p->displayId = uint32_t(s.targetId);
        p->name = s.name;
        return p;
    };

    policy.source.startCapture = [engine](deskhubp::HostSource& st) {
        SourcePipeline* p = &Pipeline(st);
        const uint32_t fps = engine->options().fps;
        p->curFps.store(fps, std::memory_order_relaxed);

        auto onPacket = [p, engine](const uint8_t* data, size_t size, uint64_t tsUs,
                            bool keyframe) {
            deskhubp::SendEncodedFrame(*p, engine->socket(),
                std::span<const uint8_t>(data, size), tsUs, keyframe);
        };

        p->ensureEncoderFn = [p, fps, onPacket](uint32_t w, uint32_t h) -> bool {
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

        auto onFrame = [p](const MacFrameInfo& fi) {
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
                p->ReleaseCached();
                p->sizeChanged.store(true, std::memory_order_release);
            }

            if (encW < deskhub::kMinEncodeWidth || encH < deskhub::kMinEncodeHeight) {
                if (!p->paused.exchange(true, std::memory_order_acq_rel))
                    LOGI(
                        "[Agent][%s] Source too small to encode (%ux%u) \xE2\x80\x94 paused, "
                        "waiting for it to grow back.",
                        p->name.c_str(), encW, encH);
                return;
            }
            if (p->paused.exchange(false, std::memory_order_acq_rel))
                LOGI("[Agent][%s] Source back to %ux%u \xE2\x80\x94 resuming.", p->name.c_str(),
                    encW, encH);

            if (p->cachedPb) CVPixelBufferRelease(static_cast<CVPixelBufferRef>(p->cachedPb));
            p->cachedPb = CVPixelBufferRetain(static_cast<CVPixelBufferRef>(fi.pixelBuffer));
            p->haveCached.store(p->cachedPb != nullptr, std::memory_order_release);
            p->lastFrameUs.store(fi.meta.timestampUs, std::memory_order_relaxed);

            if (!p->netReady.load(std::memory_order_acquire)) return;
            if (!p->ensureEncoderFn(encW, encH)) return;
            p->DiagEncode(fi.pixelBuffer, fi.meta.timestampUs, p->forceIdr.exchange(false));
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

    policy.source.inputSkipped = [](const deskhubp::HostSource& st) {
        return Pipeline(st).injector.skipped();
    };

    policy.source.takeIdleFrames = [](deskhubp::HostSource& st) {
        return Pipeline(st).capture.TakeIdleCount();
    };

    policy.source.flush = [](deskhubp::HostSource& st, uint64_t nowUs) {
        SourcePipeline& p = Pipeline(st);
        if (!p.haveCached.load(std::memory_order_acquire)) return;
        std::lock_guard<std::mutex> lk(p.encMutex);
        if (!p.cachedPb || !p.ensureEncoderFn(p.srcW.load(), p.srcH.load())) return;
        p.DiagEncode(p.cachedPb, nowUs, p.forceIdr.exchange(false));
        p.encoder->Flush();
        p.lastKeepaliveUs = nowUs;
    };

    return engine_.Start(sources, opt, std::move(policy));
}
