#include "deskhubp/session/AgentLoop.h"

#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <utility>

#include "capture/AudioCapture.h"
#include "capture/ScreenCapture.h"
#include "deskhubp/diag/Log.h"
#include "deskhubp/input/LocalInput.h"
#include "deskhubp/media/PortalScreenCast.h"
#include "encode/VaEncoder.h"
#include "input/InputInjector.h"

#include "deskhub/control/FrameGate.h"
#include "deskhub/control/StreamSize.h"
#include "deskhub/diag/AgentDiag.h"
#include "deskhub/session/HostRouter.h"

namespace {

using LinuxSourceBase = deskhubp::HostSourceBase<ScreenCapture, InputInjector, VaEncoder>;

struct SourcePipeline : LinuxSourceBase {
    SourcePipeline(uint32_t startBps, uint32_t minBps)
        : LinuxSourceBase(startBps, minBps, deskhub::diag::AgentDiagCaps{false, true}) {}

    uint32_t nodeId = 0;
    int32_t srcX = 0, srcY = 0;

    deskhub::FrameGate frameGate;
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
    auto audioCapture = std::make_shared<AudioCapture>();
    policy.startAudioCapture = [audioCapture](const deskhub::media::AudioFormat& format,
                                   std::function<void(std::span<const int16_t>)> onFrame) {
        return audioCapture->Start(format, std::move(onFrame));
    };
    policy.stopAudioCapture = [audioCapture] { audioCapture->Stop(); };
    policy.source = deskhubp::MakeDefaultSourcePolicy<SourcePipeline>();
    policy.status = deskhubp::MakeDefaultStatusHooks<SourcePipeline>();
    policy.noSourceError = "No display to share.";
    policy.noUsableSourceError =
        "No usable source \xE2\x80\x94 the compositor sent no frame.";
    policy.onApprovalNeeded = [this](uint64_t addrPacked, std::string shortKey,
                                  std::string name) {
        PushPairingRequest(PairingRequest{addrPacked, std::move(shortKey), std::move(name)});
    };

    policy.preflight = [] {
        if (deskhubp::PortalScreenCast::Instance().isOpen()) return std::string();
        return std::string(
            "The screen-capture permission is gone \xE2\x80\x94 press Share "
            "again.");
    };

    policy.status.zeroCopy = [](const deskhubp::HostSource& st) {
        return Pipeline(st).capture.usingDmaBuf();
    };

    policy.source.create = [engine](const AgentSource& s,
                               uint8_t sourceId) -> std::unique_ptr<deskhubp::HostSource> {
        auto p = deskhubp::MakeHostSource<SourcePipeline>(*engine, s, sourceId);
        p->nodeId = uint32_t(s.targetId);
        p->srcX = s.x;
        p->srcY = s.y;
        return p;
    };

    policy.source.startCapture = [engine](deskhubp::HostSource& st) {
        SourcePipeline* p = &Pipeline(st);
        const uint32_t fps = engine->options().fps;
        const uint32_t maxDim = engine->options().maxDim;

        auto onPacket = engine->MakePacketSink(*p);

        auto ensureEncoder = [p, fps, onPacket](uint32_t w, uint32_t h) -> bool {
            if (p->encoder && p->encoder->IsOpen()) return true;
            EncoderConfig cfg = deskhub::MakeEncoderConfig(*p, {w, h}, fps);
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

            std::lock_guard<std::mutex> lk(p->encMutex);

            const deskhub::FrameAdmission adm = deskhub::AdmitCapturedFrame(*p, fi.meta.width,
                fi.meta.height, maxDim);
            if (adm.rebuildEncoder) {
                p->encoder.reset();
                p->SetCachedFrame(false);
            }
            if (!adm.sizeNote.empty())
                LOGI("[Agent][%s] %s", p->name.c_str(), adm.sizeNote.c_str());
            if (!adm.pauseNote.empty())
                LOGI("[Agent][%s] %s", p->name.c_str(), adm.pauseNote.c_str());
            if (adm.drop) return;

            if (!p->frameGate.Admit(p->curFps.load(std::memory_order_relaxed),
                    fi.meta.timestampUs))
                return;

            p->lastFrameUs.store(fi.meta.timestampUs, std::memory_order_relaxed);

            if (!p->netReady.load(std::memory_order_acquire)) return;
            if (!ensureEncoder(adm.encode.width, adm.encode.height)) return;
            const bool idr = p->forceIdr.exchange(false);
            VaEncoder* enc = p->encoder.get();
            const bool ok = deskhubp::DiagEncode(*p, idr,
                [enc, &fi, idr] { return enc->Encode(fi, fi.meta.timestampUs, idr); });
            if (!ok) {
                p->encoder.reset();
                p->SetCachedFrame(false);
                p->forceIdr.store(true);
                return;
            }
            p->SetCachedFrame(enc->haveSourceFrame());
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
        p.SetCachedFrame(false);
    };

    policy.source.attachInput = [engine](deskhubp::HostSource& st) {
        SourcePipeline& p = Pipeline(st);
        const AgentOptions& o = engine->options();
        p.injector.SetLocalMonitor(&engine->localInput());
        p.injector.SetEnabled(p.injector.Init(p.srcX, p.srcY, p.nativeW.load(), p.nativeH.load(),
            o.desktopX, o.desktopY, o.desktopW, o.desktopH));
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
            p.SetCachedFrame(false);
        }
        return t;
    };

    policy.source.flush = [](deskhubp::HostSource& st, uint64_t nowUs) {
        SourcePipeline& p = Pipeline(st);
        auto lk = deskhubp::TryHoldEncoder(p.encMutex);
        if (!lk.owns_lock() || !p.encoder || !p.hasCachedFrame()) return;
        const bool idr = p.forceIdr.exchange(false);
        VaEncoder* enc = p.encoder.get();
        const bool ok = deskhubp::DiagEncode(p, idr,
            [enc, nowUs, idr] { return enc->EncodeLast(nowUs, idr); });
        if (!ok) {
            p.encoder.reset();
            p.SetCachedFrame(false);
            p.forceIdr.store(true);
        }
    };

    return engine_.Start(sources, opt, std::move(policy));
}
