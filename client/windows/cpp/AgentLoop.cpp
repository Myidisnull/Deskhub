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

#include "capture/GpuSelect.h"
#include "ElevatedShare.h"
#include "net/Firewall.h"
#include "input/InputInjector.h"
#include "deskhubp/input/LocalInput.h"
#include "encode/IVideoEncoder.h"
#include "deskhubp/net/NetInfo.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/diag/LogFile.h"
#include "deskhubp/system/Random.h"
#include "deskhubp/net/UdpSocket.h"
#include "capture/Downscaler.h"
#include "capture/ScreenCapture.h"
#include "AgentControl.h"

#include "deskhub/control/BitrateController.h"
#include "deskhub/control/QualityLadder.h"
#include "deskhub/control/StreamSize.h"
#include "deskhub/diag/AgentDiag.h"
#include "deskhub/session/Beacon.h"
#include "deskhub/session/HostFeedback.h"
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

inline constexpr uint32_t kMinEncodeW = 160;
inline constexpr uint32_t kMinEncodeH = 64;

struct SourcePipeline : deskhub::SourcePipelineState {
    SourcePipeline(uint32_t startBps, uint32_t minBps)
        : deskhub::SourcePipelineState(startBps, minBps, deskhub::diag::AgentDiagCaps{}) {}

    HMONITOR monitor = nullptr;

    ScreenCapture capture;
    InputInjector injector;

    std::atomic<uint32_t> srcTexW{0}, srcTexH{0};
    std::atomic<uint64_t> lastEncodeUs{0};

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
            std::printf("[DIAG][%s] evt=enc_fail idr=%d ms=%u\n", name.c_str(), idr ? 1 : 0, ms);
    }
};

}

int RunAgent(std::span<const AgentSource> sources, const AgentOptions& opt, AgentControl& ctl) {
    g_ctrlC.store(false);
    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    if (sources.empty()) {
        std::printf("[Agent] No source selected.\n");
        ctl.OnFailed("no source selected");
        return 1;
    }
    if (sources.size() > deskhub::kMaxSources) {
        std::printf("[Agent] At most %zu sources can be shared at once.\n", deskhub::kMaxSources);
        ctl.OnFailed("too many sources");
        return 1;
    }

    GpuChoice gpu;
    if (!CreateBestDevice({GpuVendor::Nvidia, GpuVendor::Intel, GpuVendor::Amd}, gpu)) {
        std::printf("[Agent] Failed to create D3D11 device.\n");
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
            std::printf(
                "[Agent] Cannot start sharing: UDP port %u is already in use. "
                "Another Deskhub is probably still running — close it and try again.\n",
                unsigned(kDeskhubPort));
            ctl.OnFailed("udp port 47777 is already in use");
        } else {
            std::printf("[Agent] Cannot start sharing: could not open UDP port %u.\n",
                unsigned(kDeskhubPort));
            ctl.OnFailed("could not open udp port 47777");
        }
        return 1;
    }
    sock.SetRecvTimeout(100);

    if (EnsureHostFirewallRule())
        std::printf("[Agent] Windows Firewall: inbound rule verified (all profiles).\n");
    else
        std::printf(
            "[Agent] Could not add/verify a Windows Firewall rule (needs admin). "
            "If the other machine cannot connect, allow client.exe through Windows "
            "Firewall for the current network.\n");

    std::printf(
        "[Agent] Listening on UDP port %u. On the other machine, open Deskhub"
        " and enter one of:\n",
        unsigned(kDeskhubPort));
    for (const auto& a : ListLocalIPv4())
        std::printf("    %s    (%s)\n", a.ip.c_str(), a.name.c_str());

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
                std::printf(
                    "[Agent][%s] No usable encoder backend (NVENC + Media Foundation"
                    " both failed).\n",
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

            uint32_t encW = p->wantW.load(std::memory_order_relaxed);
            uint32_t encH = p->wantH.load(std::memory_order_relaxed);
            if (!encW || !encH) {
                const deskhub::StreamSize t =
                    deskhub::FitStreamSize(fi.meta.width, fi.meta.height, maxDim, 0, 0);
                encW = t.width;
                encH = t.height;
            }
            if (encW > fi.meta.width) encW = fi.meta.width;
            if (encH > fi.meta.height) encH = fi.meta.height;
            encW &= ~1u;
            encH &= ~1u;
            if (!encW || !encH) return;

            std::lock_guard<std::mutex> lk(p->encMutex);

            if (p->srcW.load() != encW || p->srcH.load() != encH) {
                if (p->srcW.load())
                    std::printf(
                        "[Agent][%s] Encode size %ux%u -> %ux%u (source %ux%u),"
                        " rebuilding encoder.\n",
                        p->name.c_str(), p->srcW.load(), p->srcH.load(), encW, encH,
                        fi.meta.width, fi.meta.height);
                p->srcW.store(encW);
                p->srcH.store(encH);
                p->encoder.reset();
                p->cachedTex.Reset();
                p->haveCached.store(false, std::memory_order_release);
                p->sizeChanged.store(true, std::memory_order_release);
            }

            if (encW < kMinEncodeW || encH < kMinEncodeH) {
                if (!p->paused.exchange(true, std::memory_order_acq_rel))
                    std::printf(
                        "[Agent][%s] Source too small to encode (%ux%u) —"
                        " paused, waiting for it to grow back.\n",
                        p->name.c_str(), encW, encH);
                return;
            }
            if (p->paused.exchange(false, std::memory_order_acq_rel))
                std::printf("[Agent][%s] Source back to %ux%u — resuming.\n",
                    p->name.c_str(), encW, encH);

            const uint64_t frameUs = NowUs();
            if (const uint32_t gateFps = p->curFps.load(std::memory_order_relaxed)) {
                const uint64_t minGapUs = 1'000'000ull / gateFps;
                const uint64_t last = p->lastEncodeUs.load(std::memory_order_relaxed);
                if (last && frameUs > last && frameUs - last + 500 < minGapUs) return;
                p->lastEncodeUs.store(frameUs, std::memory_order_relaxed);
            }

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
            std::printf("[Agent][%s] Failed to start capture — skipping this source.\n",
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
                std::printf("[Agent][%s] No frame within 10s — not sharing this source.\n",
                    p->name.c_str());
            shutdownPipeline(p.get());
            continue;
        }
        live.push_back(p.get());
    }
    if (live.empty()) {
        std::printf("[Agent] No usable source — stopping.\n");
        ctl.OnFailed("no usable source");
        return 1;
    }

    NetAddr replyAddr;

    LocalInputMonitor localInputMon;

    deskhub::Beacon beacon;
    uint8_t beaconBuf[deskhub::kMaxDatagram];

    auto attachSession = [&](SourcePipeline* p) {
        p->offer.width = uint16_t(p->srcW.load());
        p->offer.height = uint16_t(p->srcH.load());
        p->offer.fps = uint8_t(opt.fps);
        p->offer.bitrateBps = startBitrate;
        std::printf("[Agent] Source %u \"%s\": %ux%u @%ufps, %u Mbps.\n",
            p->sourceId, p->name.c_str(), p->offer.width, p->offer.height,
            opt.fps, opt.bitrateMbps);

        p->injector.SetLocalMonitor(&localInputMon);
        p->injector.SetEnabled(p->injector.Init(p->monitor));

        deskhub::HostCallbacks cb;
        cb.send = [&sock, &replyAddr](std::span<const uint8_t> d) {
            sock.SendTo(replyAddr, d.data(), d.size());
        };
        cb.randomBytes = [](std::span<uint8_t> out) {
            return RandomBytes(out.data(), out.size());
        };
        auto retarget = [p, &opt] { return deskhub::RetargetStream(*p, opt.maxDim); };

        cb.onHello = [p, &opt, retarget](const deskhub::Hello& h) {
            deskhub::NegotiationHooks hooks;
            hooks.resolveSize = [p, retarget](uint16_t clientW, uint16_t clientH) {
                p->cliW = clientW;
                p->cliH = clientH;
                return retarget();
            };
            const deskhub::NegotiationResult r =
                deskhub::BeginNegotiation(*p, h, uint8_t(opt.fps), hooks);
            if (!r.accepted) return;
            std::printf("[Agent][%s] Quality ladder: ceiling %ux%u @%ufps, %d rung(s).\n",
                p->name.c_str(), r.size.width, r.size.height, p->step.fps, r.rungCount);
        };
        cb.onStart = [p] {
            p->forceIdr.store(true);
            std::printf("[Agent][%s] Client START — beginning video push.\n", p->name.c_str());
        };
        cb.onKeyframeRequest = [p] { p->forceIdr.store(true); };
        cb.onNack = [p, &sock](uint32_t frameId, std::span<const uint16_t> indices) {
            const NetAddr peer = NetAddr::Unpack(p->peerPacked.load(std::memory_order_acquire));
            deskhub::RespondToNack(*p, frameId, indices, [&sock, &peer](std::span<const uint8_t> d) {
                sock.SendTo(peer, d.data(), d.size());
            });
        };
        cb.onInput = [p](const deskhub::InputEvent& e) { p->injector.Apply(e); };
        cb.onFocus = [p](bool focused) {
            if (!focused) p->injector.ReleaseAll();
        };
        cb.onDisconnect = [p] {
            deskhub::ForgetPeer(*p);
            p->injector.ReleaseAll();
            std::printf("[Agent][%s] Client left (BYE/timeout).\n", p->name.c_str());
        };
        cb.onFeedback = [p, retarget](const deskhub::Feedback& fb) {
            deskhub::FeedbackHooks hooks;
            hooks.setEncoderBitrate = [p](uint32_t bitrateBps) {
                std::lock_guard<std::mutex> lk(p->encMutex);
                return p->encoder && p->encoder->SetBitrate(bitrateBps);
            };
            hooks.applyQualityStep = [p, retarget](const deskhub::QualityStep& prev,
                                         const deskhub::QualityStep& next) {
                const deskhub::StreamSize t = retarget();
                std::lock_guard<std::mutex> lk(p->encMutex);
                if (p->encoder && prev.fps != next.fps) p->encoder->SetFps(next.fps);
                return t;
            };

            const deskhub::FeedbackOutcome out = deskhub::ApplyFeedback(*p, fb, NowUs(), hooks);

            if (out.fecToggled) {
                if (out.fecEnabled)
                    std::printf("[Agent][%s] FEC on (loss %u%%).\n", p->name.c_str(), fb.lossPct);
                else
                    std::printf("[Agent][%s] FEC off (link clean).\n", p->name.c_str());
            }
            if (out.bitrateChanged)
                std::printf("[Agent][%s] Bitrate %.1f -> %.1f Mbps (loss %u%%, RTT %u ms)\n",
                    p->name.c_str(), out.previousBitrateBps / 1e6, out.bitrateBps / 1e6,
                    fb.lossPct, fb.rttMs);
            if (out.qualityChanged)
                std::printf(
                    "[Agent][%s] Quality %u%%@%ufps -> %u%%@%ufps (%ux%u, budget %.1f Mbps)\n",
                    p->name.c_str(), out.previousStep.scalePct, out.previousStep.fps,
                    out.step.scalePct, out.step.fps, out.size.width, out.size.height,
                    p->rate.bitrateBps() / 1e6);
        };

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
            const uint32_t w = p->srcW.load(), hgt = p->srcH.load();
            const uint64_t peer = p->peerPacked.load(std::memory_order_relaxed);

            SessionSourceRow r;
            r.sourceId = p->sourceId;
            wchar_t suffix[64];
            swprintf(suffix, 64, L"  (%ux%u%ls)", w, hgt,
                peer ? L", viewer connected" : L"");
            r.label = FromUtf8(p->name) + suffix;
            r.name = FromUtf8(p->name);
            r.width = w;
            r.height = hgt;
            r.viewerConnected = peer != 0;
            if (peer) r.viewerAddr = NetAddr::Unpack(peer).ToString();
            r.fps = p->uiFps.load(std::memory_order_relaxed);
            r.kbps = p->uiKbps.load(std::memory_order_relaxed);
            r.rttMs = p->uiRttMs.load(std::memory_order_relaxed);
            r.monitor = uint64_t(uintptr_t(p->monitor));
            rows.push_back(std::move(r));

            deskhub::SourceInfo si;
            si.sourceId = p->sourceId;
            si.width = uint16_t(w);
            si.height = uint16_t(hgt);
            si.name = p->name;
            infos.push_back(std::move(si));
        }
        beacon.SetSources(infos);
        ctl.SetRows(std::move(rows));
    };
    publishRows();

    localInputMon.Start();

    {
        const bool elevated = IsProcessElevated();
        std::printf("[Agent] Client control allowed (mouse + keyboard). Host elevated: %s%s\n",
            elevated ? "YES" : "NO",
            elevated ? "" : " — input will NOT reach apps running as administrator");
    }
    std::printf("[Agent] Sharing %zu source(s). Waiting for client...\n", live.size());

    uint8_t buf[deskhub::kMaxDatagram];
    uint64_t lastStatUs = NowUs();
    bool anyFailed = false;
    deskhub::diag::AgentDiag loopDiag;
    char logLine[deskhub::diag::SourceDiag::kStatusBufBytes];

    for (;;) {
        if (g_ctrlC.load()) break;
        if (ctl.stopRequested()) break;

        bool anyAlive = false;
        for (SourcePipeline* p : live)
            if (!p->failed.load() && !p->capture.Closed()) anyAlive = true;
        if (!anyAlive) break;

        NetAddr from;
        const int n = sock.RecvFrom(buf, sizeof(buf), from);
        const uint64_t now = NowUs();
        if (n < 0) {
            std::printf("[Agent] Socket error — stopping.\n");
            ctl.OnFailed("socket error");
            anyFailed = true;
            break;
        }

        if (n > 0) {
            const auto span = std::span<const uint8_t>(buf, size_t(n));
            if (const size_t rn = beacon.Reply(beaconBuf, span); rn) {
                sock.SendTo(from, beaconBuf, rn);
            } else {
                const deskhub::AcceptedDatagram acc =
                    deskhub::AcceptDatagram(liveStates, span, from.Pack(), now);
                if (acc.parsed) replyAddr = from;
                if (acc.peerChanged)
                    std::printf("[Agent][%s] Peer: %s\n", acc.target->name.c_str(),
                        from.ToString().c_str());
            }
        }

        for (SourcePipeline* p : live) {
            if (p->failed.load()) continue;
            p->session->Tick(now);

            if (const char* idrLine = p->diag.FormatIdr(logLine, sizeof(logLine),
                    p->name.c_str()))
                std::printf("%s\n", idrLine);

            const deskhub::OfferUpdate u = deskhub::RefreshOffer(*p, uint8_t(opt.fps));
            if (u.sendReconfig) {
                uint8_t rbuf[deskhub::kMaxDatagram];
                const size_t rn = deskhub::BuildReconfig(rbuf, p->session->sessionId(), u.reconfig);
                if (rn) sock.SendTo(NetAddr::Unpack(p->peerPacked.load()), rbuf, rn);
                p->forceIdr.store(true);
            }

            if (p->haveCached.load(std::memory_order_acquire) &&
                deskhub::DueForFlush(*p, now) != deskhub::FlushReason::None) {
                std::lock_guard<std::mutex> lk(p->encMutex);
                if (p->ensureEncoderFn(p->srcW.load(), p->srcH.load(),
                        p->srcTexW.load(), p->srcTexH.load())) {
                    p->DiagEncode(p->encoder.get(), p->cachedTex.Get(),
                        p->forceIdr.exchange(false));
                    p->lastKeepaliveUs = now;
                }
            }
        }

        if (now - lastStatUs >= 1'000'000) {
            const std::string hms = deskhubp::LocalTimeHms();
            for (SourcePipeline* p : live) {
                if (p->failed.load()) continue;
                const uint32_t cap = p->captured.load();
                const uint64_t by = p->bytesSent.load(), fr = p->framesSent.load();
                const auto& ist = p->session->inputStats();
                deskhub::diag::SourceDiag::Window sw;
                p->statWindow = p->statRate.Close(cap, fr, by, now);
                sw.rate = p->statWindow;
                sw.inputApplied = ist.applied;
                sw.inputLost = ist.lost;
                sw.inputSkipped = p->injector.skipped();

                deskhub::diag::SourceDiag::LinkView link;
                link.have = p->haveFeedback.load(std::memory_order_acquire);
                link.lossPct = p->uiLossPct.load(std::memory_order_relaxed);
                link.rttMs = p->uiRttMs.load(std::memory_order_relaxed);
                link.recvKbps = p->uiRecvKbps.load(std::memory_order_relaxed);

                std::printf("%s\n",
                    deskhub::diag::SourceDiag::FormatStatus(logLine, sizeof(logLine), hms.c_str(),
                        p->name.c_str(), deskhub::diag::StateName(p->session->state()), sw, link));
                p->uiFps.store(uint32_t(sw.rate.sendFps + 0.5), std::memory_order_relaxed);
                p->uiKbps.store(uint32_t(sw.rate.sendKbps + 0.5), std::memory_order_relaxed);

                std::printf("%s\n",
                    p->diag.FormatSum(logLine, sizeof(logLine), hms.c_str(), p->name.c_str(),
                        0, false));
            }
            std::printf("%s\n", loopDiag.FormatSum(logLine, sizeof(logLine), hms.c_str()));
            publishRows();
            lastStatUs = now;
        }

        const uint32_t busyMs = uint32_t((NowUs() - now) / 1000);
        loopDiag.loopBusyMs.Add(busyMs);
        if (busyMs > 250)
            std::printf("[DIAG][agent] evt=recv_stall busy_ms=%u\n", busyMs);
    }

    uint64_t totalFrames = 0;
    double totalMB = 0;
    for (auto& up : pipes) {
        shutdownPipeline(up.get());
        totalFrames += up->framesSent.load();
        totalMB += up->bytesSent.load() / 1e6;
    }
    std::printf("[Agent] Stopped. Total: %llu frames sent, %.2f MB.\n",
        (unsigned long long)totalFrames, totalMB);
    SetConsoleCtrlHandler(CtrlHandler, FALSE);
    return anyFailed ? 1 : 0;
}
