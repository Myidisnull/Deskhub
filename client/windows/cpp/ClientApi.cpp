#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include "DeskhubApi.h"

#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <cinttypes>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "capture/GpuSelect.h"
#include "decode/IVideoDecoder.h"
#include "decode/PanelRenderer.h"
#include "deskhubp/UdpSocket.h"
#include "deskhubp/Clock.h"
#include "deskhubp/LogFile.h"

#include "deskhub/control/ClockOffset.h"
#include "deskhub/control/LinkStats.h"
#include "deskhub/diag/ClientDiag.h"
#include "deskhub/session/ClientSession.h"
#include "deskhub/transport/Reassembler.h"

struct DhClientHandle {
    GpuChoice gpu;
    PanelRenderer renderer;
    UdpSocket sock;
    NetAddr server{};
    uint8_t sourceId = 0;

    DhClientStatsCallback statsCb = nullptr;
    DhClientSizeCallback sizeCb = nullptr;
    DhClientClosedCallback closedCb = nullptr;
    void* user = nullptr;

    std::atomic<bool> quit{false};
    std::atomic<bool> failed{false};
    std::atomic<bool> userStop{false};
    std::atomic<const char*> failReason{nullptr};

    std::mutex inputMutex;
    std::vector<deskhub::InputEvent> inputQueue;

    std::thread thread;

    void Run();
    void PushInput(const deskhub::InputEvent& e) {
        std::lock_guard<std::mutex> lk(inputMutex);
        inputQueue.push_back(e);
    }
};

void DhClientHandle::Run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    std::unique_ptr<deskhub::Reassembler> reasm;
    std::unique_ptr<IVideoDecoder> decoder;
    std::atomic<uint32_t> decW{0}, decH{0}, decFps{0};

    std::atomic<int64_t> lastE2eUs{-1};
    deskhub::ClockOffset clockOffset;

    uint64_t stBytes = 0;
    std::atomic<uint32_t> stRendered{0};

    deskhub::diag::ClientDiag diag{deskhub::diag::ClientDiagCaps{
        true, false}};

    constexpr size_t kMaxQueuedFrames = 3;
    std::mutex decQueueMutex;
    std::condition_variable decQueueCv;
    struct QItem {
        deskhub::Reassembler::Frame frame;
        uint64_t enqUs = 0;
    };
    std::deque<QItem> decQueue;
    std::atomic<bool> decodeThreadStop{false};
    std::atomic<bool> decodeFailedFlag{false};
    std::atomic<bool> queueOverflowFlag{false};
    bool negotiated = false;

    auto onDecoded = [&](const DecodedFrame& df) {
        uint64_t readyUs = 0;
        if (!renderer.RenderNV12(df.texture, df.subresource, df.width, df.height, &readyUs))
            return;
        stRendered.fetch_add(1, std::memory_order_relaxed);

        if (readyUs) {
            diag.presentMs.Add(uint32_t((NowUs() - readyUs) / 1000));
        }

        if (df.timestampUs) {
            clockOffset.AddSample(df.timestampUs, readyUs ? readyUs : NowUs());
            lastE2eUs.store(
                clockOffset.LatencyUs(diag.minRttUs.value() / 2));
        }
    };

    std::thread decodeThread([&] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        for (;;) {
            QItem it;
            {
                std::unique_lock<std::mutex> lk(decQueueMutex);
                decQueueCv.wait(lk, [&] { return decodeThreadStop.load() || !decQueue.empty(); });
                if (decQueue.empty()) {
                    if (decodeThreadStop.load()) break;
                    continue;
                }
                it = std::move(decQueue.front());
                decQueue.pop_front();
            }
            if (!decoder) {
                DecoderConfig dc;
                dc.codec = Codec::H264;
                dc.width = decW.load(std::memory_order_relaxed);
                dc.height = decH.load(std::memory_order_relaxed);
                dc.fps = decFps.load(std::memory_order_relaxed);
                decoder = CreateDecoder(gpu.device.Get(), dc, onDecoded);
                if (!decoder) {
                    failReason.store("decoder init failed");
                    failed.store(true);
                    break;
                }
            }
            deskhub::Reassembler::Frame& f = it.frame;
            const uint64_t t0 = NowUs();
            const bool ok = decoder->Decode(f.nal.data(), f.nal.size(), f.timestampUs);
            const uint32_t decMs = uint32_t((NowUs() - t0) / 1000);
            diag.decMs.Add(decMs);
            if (!ok) decodeFailedFlag.store(true, std::memory_order_release);
        }
        CoUninitialize();
    });

    deskhub::ClientCallbacks cb;
    cb.send = [&](std::span<const uint8_t> d) { sock.SendTo(server, d.data(), d.size()); };
    cb.onReady = [&](const deskhub::NegotiatedParams& np) {
        negotiated = true;
        decW.store(np.width, std::memory_order_relaxed);
        decH.store(np.height, std::memory_order_relaxed);
        decFps.store(np.fps ? np.fps : 60, std::memory_order_relaxed);
        if (sizeCb) sizeCb(np.width, np.height, user);
    };
    cb.onReconfig = [&](const deskhub::NegotiatedParams& np) {
        if (reasm) reasm->SetFps(np.fps);
        decFps.store(np.fps ? np.fps : decFps.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        decW.store(np.width, std::memory_order_relaxed);
        decH.store(np.height, std::memory_order_relaxed);
        if (sizeCb) sizeCb(np.width, np.height, user);
    };
    cb.onRtt = [&](uint32_t rttUs) { diag.minRttUs.Add(rttUs); };
    bool closedNotified = false;
    cb.onDisconnect = [&](const char* reason) {
        closedNotified = true;
        if (closedCb) closedCb(reason ? reason : "disconnected", user);
        quit.store(true);
    };

    deskhub::ClientSession session(cb);

    deskhub::Hello hello;
    hello.clientId = uint32_t(NowUs()) ^ GetCurrentProcessId() ^ (uint32_t(sourceId) << 24);
    hello.codecMask = deskhub::kCodecMaskH264;
    hello.maxWidth = uint16_t(GetSystemMetrics(SM_CXVIRTUALSCREEN));
    hello.maxHeight = uint16_t(GetSystemMetrics(SM_CYVIRTUALSCREEN));
    hello.desiredFps = 60;
    hello.features = 0;
    hello.sourceId = sourceId;
    session.Start(hello, NowUs());

    uint8_t buf[deskhub::kMaxDatagram];
    deskhub::LinkStats linkStats(NowUs());
    deskhub::diag::KeyframeRequestLog kfLog;
    char kfLine[deskhub::diag::KeyframeRequestLog::kBufBytes];

    while (!quit.load() && !failed.load()) {
        NetAddr from;
        const int n = sock.RecvFrom(buf, sizeof(buf), from);
        const uint64_t now = NowUs();
        if (n < 0) {
            failReason.store("socket error");
            failed.store(true);
            break;
        }

        if (n > 0) {
            const auto span = std::span<const uint8_t>(buf, size_t(n));
            const auto h = deskhub::ParseCommonHeader(span);
            if (h && h->chan == deskhub::Chan::Video) {
                if (h->sessionId == session.sessionId() && session.sessionId() != 0) {
                    const auto pl = deskhub::PayloadOf(span);
                    if (!reasm) {
                        const uint32_t fps = session.params().fps ? session.params().fps : 60;
                        reasm = std::make_unique<deskhub::Reassembler>(1'000'000 / fps);
                        reasm->onFrameDrop = [](const deskhub::Reassembler::FrameDropInfo& d) {
                            static const char* const kReason[] =
                                {"timeout", "overtaken", "evicted", "pre_idr"};
                            const char* pos = "-";
                            if (d.missing) {
                                const bool head = d.firstMissing == 0;
                                const bool tail = d.lastMissing + 1 == d.total;
                                pos = head && tail ? "all" : tail ? "tail"
                                                         : head   ? "head"
                                                                  : "mid";
                            }
                            std::printf(
                                "[DIAG] evt=frame_drop id=%u reason=%s miss=%u/%u pos=%s"
                                " idr=%u waited_ms=%u got_bytes=%u\n",
                                d.frameId, kReason[size_t(d.reason)], d.missing, d.total,
                                pos, d.idr ? 1 : 0, d.waitedMs, d.bytesGot);
                        };
                        decW.store(session.params().width, std::memory_order_relaxed);
                        decH.store(session.params().height, std::memory_order_relaxed);
                        decFps.store(fps, std::memory_order_relaxed);
                    }
                    if (h->type == deskhub::MsgType::FecPacket) {
                        if (const auto v = deskhub::ParseFecPacket(*h, pl)) {
                            session.NotifyVideoPacket(now);
                            reasm->PushFec(*v, now);
                            stBytes += v->parity.size();
                        }
                    } else if (const auto v = deskhub::ParseVideoPacket(*h, pl)) {
                        session.NotifyVideoPacket(now);
                        reasm->Push(*v, now);
                        stBytes += v->payload.size();
                    }
                }
            } else if (h) {
                session.HandlePacket(span, now);
            }
        }

        auto requestKf = [&](const char* reason) {
            if (const char* l = kfLog.Request(kfLine, sizeof(kfLine), now, reason))
                std::printf("%s\\n", l);
            session.RequestKeyframe();
        };

        if (reasm) {
            while (auto f = reasm->PopReady(now)) {
                if (f->idr) {
                    session.CancelKeyframeRequest();
                    if (const char* l = kfLog.Arrived(kfLine, sizeof(kfLine), now, f->nal.size()))
                        std::printf("%s\\n", l);
                }
                if (f->firstSeenUs) {
                    const uint32_t asmMs = uint32_t((now - f->firstSeenUs) / 1000);
                    diag.asmMs.Add(asmMs);
                }
                {
                    std::lock_guard<std::mutex> lk(decQueueMutex);
                    if (decQueue.size() >= kMaxQueuedFrames) {
                        decQueue.pop_front();
                        queueOverflowFlag.store(true, std::memory_order_release);
                        diag.dqDrop.Add();
                    }
                    decQueue.push_back(QItem{std::move(*f), now});
                }
                decQueueCv.notify_one();
            }
            if (reasm->TakeLossEvent())
                requestKf("loss");
            else if (reasm->WaitingForIdr())
                requestKf("wait_idr");
        }
        if (decodeFailedFlag.exchange(false, std::memory_order_acq_rel)) requestKf("dec_fail");
        if (queueOverflowFlag.exchange(false, std::memory_order_acq_rel)) requestKf("q_overflow");

        if (reasm) {
            uint16_t nackIdx[64];
            uint32_t nackFrame = 0;
            const size_t nn = reasm->PlanNack(now, diag.minRttUs.value(),
                nackFrame, nackIdx);
            if (nn) session.SendNack(nackFrame, std::span<const uint16_t>(nackIdx, nn));
        }

        {
            std::vector<deskhub::InputEvent> batch;
            {
                std::lock_guard<std::mutex> lk(inputMutex);
                batch.swap(inputQueue);
            }
            for (const auto& e : batch) session.QueueInput(e);
        }

        session.SetFocused(true);
        session.Tick(now);
        if (session.state() == deskhub::ClientSession::State::Dead) break;

        if (linkStats.Due(now)) {
            const auto st = reasm ? reasm->stats() : deskhub::Reassembler::Stats{};
            const uint32_t rendered = stRendered.exchange(0, std::memory_order_relaxed);
            const deskhub::LinkWindow w = linkStats.Close(st, stBytes, rendered, now);
            const int64_t e2e = lastE2eUs.load();
            session.SendFeedback(deskhub::MakeFeedback(w, session.lastRttUs()));

            if (negotiated && statsCb) {
                char line[deskhub::diag::ClientDiag::kCompactBufBytes];
                statsCb(deskhub::diag::ClientDiag::FormatCompact(line, sizeof(line), w,
                            session.lastRttUs(), e2e, " \xC2\xB7 "),
                    user);
            }

            const std::string hms = deskhubp::LocalTimeHms();
            char logLine[deskhub::diag::ClientDiag::kSumBufBytes];
            std::printf("%s\n", deskhub::diag::ClientDiag::FormatStatus(logLine, sizeof(logLine),
                                    hms.c_str(), w, session.lastRttUs(), e2e));
            std::printf("%s\n", diag.FormatSum(logLine, sizeof(logLine), hms.c_str(), w,
                                    reasm ? reasm->TakeMaxGapMs() : 0, e2e));

            stBytes = 0;
        }

        const uint32_t busyMs = uint32_t((NowUs() - now) / 1000);
        diag.loopBusyMs.Add(busyMs);
        if (busyMs > 50) std::printf("[DIAG] evt=recv_stall busy_ms=%u\n", busyMs);
    }

    decodeThreadStop.store(true);
    decQueueCv.notify_one();
    decodeThread.join();

    session.SendBye();
    quit.store(true);

    if (!closedNotified && !userStop.load()) {
        const char* r = failReason.load();
        if (closedCb) closedCb(r ? r : "connection lost", user);
    }
    CoUninitialize();
}

namespace {

deskhub::InputEvent MakeMove(uint16_t nx, uint16_t ny) {
    deskhub::InputEvent e;
    e.type = deskhub::InputType::MouseMove;
    e.timestampUs = NowUs();
    e.a = nx;
    e.b = ny;
    e.absolute = 1;
    return e;
}

deskhub::InputEvent MakeMoveRel(int dx, int dy) {
    deskhub::InputEvent e;
    e.type = deskhub::InputType::MouseMove;
    e.timestampUs = NowUs();
    e.a = dx;
    e.b = dy;
    e.absolute = 0;
    return e;
}

}

namespace {

DhClientHandle* StartClient(const char* addrUtf8, uint8_t sourceId,
    uint64_t hwnd, DhClientStatsCallback statsCb, DhClientSizeCallback sizeCb,
    DhClientClosedCallback closedCb, void* user) {
    if (!addrUtf8) return nullptr;

    NetAddr server{};
    if (!ParseNetAddr(addrUtf8, server)) return nullptr;

    auto* h = new DhClientHandle();
    h->server = server;
    h->sourceId = sourceId;
    h->statsCb = statsCb;
    h->sizeCb = sizeCb;
    h->closedCb = closedCb;
    h->user = user;

    if (!CreateBestDevice({GpuVendor::Nvidia, GpuVendor::Intel, GpuVendor::Amd}, h->gpu)) {
        delete h;
        return nullptr;
    }
    if (!h->renderer.InitForHwnd(h->gpu.device.Get(), (void*)(uintptr_t)hwnd, 1280, 720)) {
        delete h;
        return nullptr;
    }
    if (!h->sock.Open(0)) {
        delete h;
        return nullptr;
    }
    h->sock.SetRecvTimeout(10);

    h->thread = std::thread([h] { h->Run(); });
    return h;
}

}

DH_API DhClientHandle* DH_CALL dh_client_start_hwnd(const char* addrUtf8, uint8_t sourceId,
    uint64_t hwnd, DhClientStatsCallback statsCb, DhClientSizeCallback sizeCb,
    DhClientClosedCallback closedCb, void* user) {
    if (!hwnd) return nullptr;
    return StartClient(addrUtf8, sourceId, hwnd, statsCb, sizeCb, closedCb, user);
}

DH_API void DH_CALL dh_client_mouse_move(DhClientHandle* h, uint16_t nx, uint16_t ny) {
    if (h) h->PushInput(MakeMove(nx, ny));
}

DH_API void DH_CALL dh_client_mouse_move_rel(DhClientHandle* h, int dx, int dy) {
    if (h) h->PushInput(MakeMoveRel(dx, dy));
}

DH_API void DH_CALL dh_client_mouse_button(DhClientHandle* h, int button, int down) {
    if (!h) return;
    deskhub::InputEvent e;
    e.type = deskhub::InputType::MouseButton;
    e.timestampUs = NowUs();
    e.a = int32_t(button == 1   ? deskhub::MouseButton::Right
                  : button == 2 ? deskhub::MouseButton::Middle
                  : button == 3 ? deskhub::MouseButton::X1
                  : button == 4 ? deskhub::MouseButton::X2
                                : deskhub::MouseButton::Left);
    e.state = down ? 1 : 0;
    h->PushInput(e);
}

DH_API void DH_CALL dh_client_wheel(DhClientHandle* h, int delta) {
    if (!h) return;
    deskhub::InputEvent e;
    e.type = deskhub::InputType::MouseWheel;
    e.timestampUs = NowUs();
    e.b = delta;
    h->PushInput(e);
}

DH_API void DH_CALL dh_client_key(DhClientHandle* h, int vk, int scan, int down) {
    if (!h) return;
    deskhub::InputEvent e;
    e.type = deskhub::InputType::Key;
    e.timestampUs = NowUs();
    e.a = vk;
    e.b = scan;
    e.state = down ? 1 : 0;
    h->PushInput(e);
}

DH_API void DH_CALL dh_client_stop(DhClientHandle* h) {
    if (!h) return;
    h->userStop.store(true);
    h->quit.store(true);
    if (h->thread.joinable()) h->thread.join();
    delete h;
}
