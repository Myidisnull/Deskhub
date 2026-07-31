#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include "AgentLoop.h"

#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <wrl/client.h>

#include "deskhubp/diag/Log.h"
#include "capture/GpuSelect.h"
#include "ElevatedShare.h"
#include "net/Firewall.h"
#include "input/InputInjector.h"
#include "deskhubp/input/LocalInput.h"
#include "encode/IVideoEncoder.h"
#include "deskhubp/net/NetInfo.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/diag/LogFile.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/session/HostNetLoop.h"
#include "capture/Downscaler.h"
#include "capture/ScreenCapture.h"
#include "AgentControl.h"

#include "deskhub/control/BitrateController.h"
#include "deskhub/control/FrameGate.h"
#include "deskhub/control/QualityLadder.h"
#include "deskhub/control/StreamSize.h"
#include "deskhub/diag/AgentDiag.h"
#include "deskhub/session/Beacon.h"
#include "deskhub/session/HostRouter.h"
#include "deskhub/session/HostSession.h"
#include "deskhub/session/SourcePipelineState.h"
#include "deskhub/transport/Packetizer.h"
#include "deskhub/transport/RetransmitCache.h"

namespace {

std::atomic<bool> g_ctrlC{false};

BOOL WINAPI CtrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        g_ctrlC.store(true);
        return TRUE;
    }
    return FALSE;
}

std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
    return w;
}

struct SourcePipeline : deskhub::SourcePipelineState {
    SourcePipeline(uint32_t startBps, uint32_t minBps)
        : deskhub::SourcePipelineState(startBps, minBps, deskhub::diag::AgentDiagCaps{}) {}

    HMONITOR monitor = nullptr;

    ScreenCapture capture;
    InputInjector injector;

    std::atomic<uint32_t> srcTexW{0}, srcTexH{0};
    deskhub::FrameGate frameGate;

    Downscaler scaler;

    std::atomic<uint32_t> uiFps{0}, uiKbps{0};

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
        if (!ok)
            LOGW("[DIAG][%s] evt=enc_fail idr=%d ms=%u", name.c_str(), idr ? 1 : 0, ms);
    }
};

}

int RunAgent(std::span<const AgentSource> sources, const AgentOptions& opt, AgentControl& ctl) {
    g_ctrlC.store(false);
    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    if (sources.empty()) {
        LOGE("[Agent] No source selected.");
        ctl.OnFailed("no source selected");
        return 1;
    }
    if (sources.size() > deskhub::kMaxSources) {
        LOGE("[Agent] At most %zu sources can be shared at once.", deskhub::kMaxSources);
        ctl.OnFailed("too many sources");
        return 1;
    }

    GpuChoice gpu;
    if (!CreateBestDevice({GpuVendor::Nvidia, GpuVendor::Intel, GpuVendor::Amd}, gpu)) {
        LOGE("[Agent] Failed to create D3D11 device.");
        ctl.OnFailed("gpu init failed");
        return 1;
    }
    std::wprintf(L"[Agent] GPU: %ls [%ls]\n", gpu.description.c_str(), GpuVendorName(gpu.vendor));
    {
        Microsoft::WRL::ComPtr<ID3D10Multithread> mt;
        if (SUCCEEDED(gpu.device.As(&mt))) mt->SetMultithreadProtected(TRUE);
    }

    UdpSocket sock;
    if (!sock.Open(kDeskhubPort)) {
        if (sock.lastBindAddrInUse()) {
            LOGE(
                "[Agent] Cannot start sharing: UDP port %u is already in use. "
                "Another Deskhub is probably still running — close it and try again.",
                unsigned(kDeskhubPort));
            ctl.OnFailed("udp port 47777 is already in use");
        } else {
            LOGE("[Agent] Cannot start sharing: could not open UDP port %u.",
                unsigned(kDeskhubPort));
            ctl.OnFailed("could not open udp port 47777");
        }
        return 1;
    }
    sock.SetRecvTimeout(100);

    if (EnsureHostFirewallRule())
        LOGI("[Agent] Windows Firewall: inbound rule verified (all profiles).");
    else
        LOGW(
            "[Agent] Could not add/verify a Windows Firewall rule (needs admin). "
            "If the other machine cannot connect, allow client.exe through Windows "
            "Firewall for the current network.");

    LOGI(
        "[Agent] Listening on UDP port %u. On the other machine, open Deskhub"
        " and enter one of:",
        unsigned(kDeskhubPort));
    for (const auto& a : ListLocalIPv4())
        LOGI("    %s    (%s)", a.ip.c_str(), a.name.c_str());

    const uint32_t startBitrate = opt.bitrateMbps * 1'000'000u;
    const uint32_t maxBitrate = startBitrate;
    const uint32_t minBitrate = 1'000'000u;

    ctl.OnBound();

    std::vector<std::unique_ptr<SourcePipeline>> pipes;

    uint8_t nextSourceId = 0;
    auto makePipeline = [&](const AgentSource& s) -> SourcePipeline* {
        auto p = std::make_unique<SourcePipeline>(startBitrate, minBitrate);
        p->sourceId = nextSourceId++;
        p->monitor = (HMONITOR)(uintptr_t)s.targetId;
        p->name = s.name;
        pipes.push_back(std::move(p));
        return pipes.back().get();
    };
    for (const AgentSource& s : sources) makePipeline(s);

    auto startPipeline = [&sock, &gpu, &opt](SourcePipeline* p) {
        auto onPacket = [p, &sock](const uint8_t* data, size_t size, uint64_t tsUs,
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
                [p, &sock, &peer](std::span<const uint8_t> d) {
                    if (sock.SendTo(peer, d.data(), d.size()))
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

        auto ensureEncoder = [p, &gpu, &opt, onPacket](uint32_t w, uint32_t h,
                                 uint32_t sw, uint32_t sh) -> bool {
            if (p->encoder) return true;
            EncoderConfig cfg;
            cfg.width = w;
            cfg.height = h;
            cfg.srcWidth = sw;
            cfg.srcHeight = sh;
            cfg.fps = p->curFps.load(std::memory_order_relaxed);
            if (!cfg.fps) cfg.fps = opt.fps;
            cfg.bitrateBps = p->curBitrateBps.load(std::memory_order_relaxed);
            cfg.outputPath.clear();
            cfg.onPacket = onPacket;
            p->encoder = CreateEncoder(gpu.device.Get(), cfg);
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
        auto onFrame = [p, &gpu, ensureEncoder, maxDim](const FrameInfo& fi) {
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
                if (!p->scaler.Configure(gpu.device.Get(), fi.meta.width, fi.meta.height, encW, encH))
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
                if (FAILED(gpu.device->CreateTexture2D(&d, nullptr, p->cachedTex.GetAddressOf())))
                    p->cachedTex.Reset();
            }
            if (p->cachedTex) {
                gpu.context->CopyResource(p->cachedTex.Get(), encTex);
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
            LOGW("[Agent][%s] Failed to start capture — skipping this source.",
                p->name.c_str());
            p->failed.store(true);
        }
    };

    auto shutdownPipeline = [&sock](SourcePipeline* p) {
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
    };

    for (auto& up : pipes) startPipeline(up.get());

    for (int i = 0; i < 1000 && !g_ctrlC.load() && !ctl.stopRequested(); ++i) {
        bool allKnown = true;
        for (auto& p : pipes)
            if (!p->failed.load() && !p->srcW.load() && !p->capture.Closed()) allKnown = false;
        if (allKnown) break;
        Sleep(10);
    }

    std::vector<SourcePipeline*> live;
    for (auto& p : pipes) {
        if (p->failed.load() || !p->srcW.load()) {
            if (!p->failed.load())
                LOGW("[Agent][%s] No frame within 10s — not sharing this source.",
                    p->name.c_str());
            shutdownPipeline(p.get());
            continue;
        }
        live.push_back(p.get());
    }
    if (live.empty()) {
        LOGE("[Agent] No usable source — stopping.");
        ctl.OnFailed("no usable source");
        return 1;
    }

    NetAddr replyAddr;

    LocalInputMonitor localInputMon;

    deskhub::Beacon beacon;

    auto attachSession = [&](SourcePipeline* p) {
        p->offer.width = uint16_t(p->srcW.load());
        p->offer.height = uint16_t(p->srcH.load());
        p->offer.fps = uint8_t(opt.fps);
        p->offer.bitrateBps = startBitrate;
        LOGI("[Agent] Source %u \"%s\": %ux%u @%ufps, %u Mbps.",
            p->sourceId, p->name.c_str(), p->offer.width, p->offer.height,
            opt.fps, opt.bitrateMbps);

        p->injector.SetLocalMonitor(&localInputMon);
        p->injector.SetEnabled(p->injector.Init(p->monitor));

        deskhubp::HostSessionHooks hooks;
        hooks.fps = opt.fps;
        hooks.send = [&sock, &replyAddr](std::span<const uint8_t> d) {
            sock.SendTo(replyAddr, d.data(), d.size());
        };
        hooks.sendToPeer = [p, &sock](std::span<const uint8_t> d) {
            sock.SendTo(NetAddr::Unpack(p->peerPacked.load(std::memory_order_acquire)), d.data(),
                d.size());
        };
        hooks.retarget = [p, &opt] { return deskhub::RetargetStream(*p, opt.maxDim); };
        hooks.applyInput = [p](const deskhub::InputEvent& e) { p->injector.Apply(e); };
        hooks.releaseInput = [p] { p->injector.ReleaseAll(); };
        hooks.setEncoderBitrate = [p](uint32_t bitrateBps) {
            std::lock_guard<std::mutex> lk(p->encMutex);
            return p->encoder && p->encoder->SetBitrate(bitrateBps);
        };
        hooks.applyQualityStep = [p, &opt](const deskhub::QualityStep& prev,
                                     const deskhub::QualityStep& next) {
            const deskhub::StreamSize t = deskhub::RetargetStream(*p, opt.maxDim);
            std::lock_guard<std::mutex> lk(p->encMutex);
            if (p->encoder && prev.fps != next.fps) p->encoder->SetFps(next.fps);
            return t;
        };

        const deskhub::HostCallbacks cb = deskhubp::MakeHostCallbacks(*p, std::move(hooks));

        p->session = std::make_unique<deskhub::HostSession>(cb, p->offer);
        p->netReady.store(true, std::memory_order_release);
    };
    for (SourcePipeline* p : live) attachSession(p);

    const std::vector<deskhub::SourcePipelineState*> liveStates(live.begin(), live.end());

    auto publishRows = [&] {
        std::vector<SessionSourceRow> rows;
        std::vector<deskhub::SourceInfo> infos;
        for (SourcePipeline* p : live) {
            if (p->failed.load() || p->capture.Closed()) continue;
            const uint64_t peer = p->peerPacked.load(std::memory_order_relaxed);

            deskhub::StatusExtras extras;
            if (peer) extras.viewerAddr = NetAddr::Unpack(peer).ToString();
            const deskhub::media::AgentSourceStatus status =
                deskhub::MakeSourceStatus(*p, extras);

            SessionSourceRow r;
            r.sourceId = status.sourceId;
            wchar_t suffix[64];
            swprintf(suffix, 64, L"  (%ux%u%ls)", status.width, status.height,
                status.viewerConnected ? L", viewer connected" : L"");
            r.label = FromUtf8(status.name) + suffix;
            r.name = FromUtf8(status.name);
            r.width = status.width;
            r.height = status.height;
            r.viewerConnected = status.viewerConnected;
            r.viewerAddr = status.viewerAddr;
            r.fps = p->uiFps.load(std::memory_order_relaxed);
            r.kbps = p->uiKbps.load(std::memory_order_relaxed);
            r.rttMs = status.rttMs;
            r.monitor = uint64_t(uintptr_t(p->monitor));
            rows.push_back(std::move(r));

            infos.push_back(deskhub::MakeSourceInfo(*p));
        }
        beacon.SetSources(infos);
        ctl.SetRows(std::move(rows));
    };
    publishRows();

    localInputMon.Start();

    {
        const bool elevated = IsProcessElevated();
        LOGI("[Agent] Client control allowed (mouse + keyboard). Host elevated: %s%s",
            elevated ? "YES" : "NO",
            elevated ? "" : " — input will NOT reach apps running as administrator");
    }
    LOGI("[Agent] Sharing %zu source(s). Waiting for client...", live.size());

    bool anyFailed = false;

    deskhubp::HostNetLoopHooks loop;
    loop.fallbackFps = opt.fps;
    loop.stopped = [&ctl] { return g_ctrlC.load() || ctl.stopRequested(); };
    loop.onPeerDatagram = [&replyAddr](const NetAddr& from) { replyAddr = from; };
    loop.publishStatus = publishRows;
    loop.onSocketError = [&ctl, &anyFailed] {
        ctl.OnFailed("socket error");
        anyFailed = true;
    };
    loop.source.closed = [](const deskhub::SourcePipelineState& st) {
        return static_cast<const SourcePipeline&>(st).capture.Closed();
    };
    loop.source.shutdown = [&shutdownPipeline](deskhub::SourcePipelineState& st) {
        shutdownPipeline(static_cast<SourcePipeline*>(&st));
    };
    loop.source.inputSkipped = [](const deskhub::SourcePipelineState& st) {
        return static_cast<const SourcePipeline&>(st).injector.skipped();
    };
    loop.source.onWindowClosed = [](deskhub::SourcePipelineState& st) {
        auto& p = static_cast<SourcePipeline&>(st);
        p.uiFps.store(uint32_t(p.statWindow.sendFps + 0.5), std::memory_order_relaxed);
        p.uiKbps.store(uint32_t(p.statWindow.sendKbps + 0.5), std::memory_order_relaxed);
    };
    loop.source.flush = [](deskhub::SourcePipelineState& st, uint64_t nowUs) {
        auto& p = static_cast<SourcePipeline&>(st);
        if (!p.haveCached.load(std::memory_order_acquire)) return;
        std::lock_guard<std::mutex> lk(p.encMutex);
        if (!p.ensureEncoderFn(p.srcW.load(), p.srcH.load(), p.srcTexW.load(), p.srcTexH.load()))
            return;
        p.DiagEncode(p.encoder.get(), p.cachedTex.Get(), p.forceIdr.exchange(false));
        p.lastKeepaliveUs = nowUs;
    };

    deskhubp::RunHostNetLoop(sock, beacon, liveStates, loop);

    uint64_t totalFrames = 0;
    double totalMB = 0;
    for (auto& up : pipes) {
        shutdownPipeline(up.get());
        totalFrames += up->framesSent.load();
        totalMB += up->bytesSent.load() / 1e6;
    }
    LOGI("[Agent] Stopped. Total: %llu frames sent, %.2f MB.",
        (unsigned long long)totalFrames, totalMB);
    SetConsoleCtrlHandler(CtrlHandler, FALSE);
    return anyFailed ? 1 : 0;
}
