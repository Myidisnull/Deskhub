#include "deskhubp/session/AgentLoop.h"

#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <utility>

#include "capture/ScreenCapture.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/input/LocalInput.h"
#include "deskhubp/media/PortalScreenCast.h"
#include "deskhubp/system/Clock.h"
#include "encode/VaEncoder.h"
#include "input/InputInjector.h"

#include "deskhub/control/FrameGate.h"
#include "deskhub/control/StreamSize.h"
#include "deskhub/diag/AgentDiag.h"
#include "deskhub/session/HostRouter.h"

namespace {

struct SourcePipeline : deskhub::SourcePipelineState {
    SourcePipeline(uint32_t startBps, uint32_t minBps)
        : deskhub::SourcePipelineState(startBps, minBps,
              deskhub::diag::AgentDiagCaps{false, true}) {}

    uint32_t nodeId = 0;
    int32_t srcX = 0, srcY = 0;

    ScreenCapture capture;
    InputInjector injector;

    deskhub::FrameGate frameGate;

    std::mutex encMutex;
    std::unique_ptr<VaEncoder> encoder;

    void DiagEncode(const std::function<bool()>& doEncode, bool idr) {
        const uint64_t t0 = NowUs();
        const bool ok = doEncode();
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
    policy.noUsableSourceError =
        "No usable source \xE2\x80\x94 the compositor sent no frame.";

    policy.preflight = [] {
        if (deskhubp::PortalScreenCast::Instance().isOpen()) return std::string();
        return std::string(
            "The screen-capture permission is gone \xE2\x80\x94 press Share "
            "again.");
    };

    policy.status.closed = [](const deskhubp::HostSource& st) {
        return Pipeline(st).capture.Closed();
    };
    policy.status.zeroCopy = [](const deskhubp::HostSource& st) {
        return Pipeline(st).capture.usingDmaBuf();
    };

    policy.source.create = [engine](const AgentSource& s,
                               uint8_t sourceId) -> std::unique_ptr<deskhubp::HostSource> {
        auto p = std::make_unique<SourcePipeline>(engine->startBitrateBps(),
            deskhubp::HostEngine::kMinBitrateBps);
        p->sourceId = sourceId;
        p->nodeId = uint32_t(s.targetId);
        p->name = s.name;
        p->srcX = s.x;
        p->srcY = s.y;
        return p;
    };

    policy.source.startCapture = [engine](deskhubp::HostSource& st) {
        SourcePipeline* p = &Pipeline(st);
        const uint32_t fps = engine->options().fps;
        const uint32_t maxDim = engine->options().maxDim;

        auto onPacket = [p, engine](const uint8_t* data, size_t size, uint64_t tsUs,
                            bool keyframe) {
            deskhubp::SendEncodedFrame(*p, engine->socket(),
                std::span<const uint8_t>(data, size), tsUs, keyframe);
        };

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

        auto onFrame = [p, ensureEncoder, maxDim](const LinuxFrameInfo& fi) {
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
                        "[Agent][%s] Encode size %ux%u -> %ux%u (source %ux%u), rebuilding "
                        "encoder.",
                        p->name.c_str(), p->srcW.load(), p->srcH.load(), encW, encH,
                        fi.meta.width, fi.meta.height);
                p->srcW.store(encW);
                p->srcH.store(encH);
                p->encoder.reset();
                p->sizeChanged.store(true, std::memory_order_release);
            }

            if (target.tooSmall) {
                if (!p->paused.exchange(true, std::memory_order_acq_rel))
                    LOGI("[Agent][%s] Source too small to encode (%ux%u) \xE2\x80\x94 paused.",
                        p->name.c_str(), encW, encH);
                return;
            }
            if (p->paused.exchange(false, std::memory_order_acq_rel))
                LOGI("[Agent][%s] Source back to %ux%u \xE2\x80\x94 resuming.", p->name.c_str(),
                    encW, encH);

            if (!p->frameGate.Admit(p->curFps.load(std::memory_order_relaxed),
                    fi.meta.timestampUs))
                return;

            p->lastFrameUs.store(fi.meta.timestampUs, std::memory_order_relaxed);

            if (!p->netReady.load(std::memory_order_acquire)) return;
            if (!ensureEncoder(encW, encH)) return;
            const bool idr = p->forceIdr.exchange(false);
            VaEncoder* enc = p->encoder.get();
            p->DiagEncode([enc, &fi, idr] { return enc->Encode(fi, fi.meta.timestampUs, idr); },
                idr);
        };

        if (!p->capture.Start(p->nodeId, deskhub::media::CaptureOptions{fps, maxDim}, onFrame)) {
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
    };

    policy.source.attachInput = [engine](deskhubp::HostSource& st) {
        SourcePipeline& p = Pipeline(st);
        const AgentOptions& o = engine->options();
        p.injector.SetLocalMonitor(&engine->localInput());
        p.injector.SetEnabled(p.injector.Init(p.srcX, p.srcY, p.nativeW.load(), p.nativeH.load(),
            o.desktopX, o.desktopY, o.desktopW, o.desktopH));
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
        if (prev.fps != next.fps) {
            std::lock_guard<std::mutex> lk(p.encMutex);
            p.encoder.reset();
        }
        return t;
    };

    policy.source.inputSkipped = [](const deskhubp::HostSource& st) {
        return Pipeline(st).injector.skipped();
    };

    policy.source.flush = [](deskhubp::HostSource& st, uint64_t nowUs) {
        SourcePipeline& p = Pipeline(st);
        std::lock_guard<std::mutex> lk(p.encMutex);
        if (!p.encoder || !p.encoder->haveSourceFrame()) return;
        const bool idr = p.forceIdr.exchange(false);
        VaEncoder* enc = p.encoder.get();
        p.DiagEncode([enc, nowUs, idr] { return enc->EncodeLast(nowUs, idr); }, idr);
        p.lastKeepaliveUs = nowUs;
    };

    return engine_.Start(sources, opt, std::move(policy));
}
