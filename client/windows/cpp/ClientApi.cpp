#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include "DeskhubApi.h"

#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "capture/GpuSelect.h"
#include "decode/IVideoDecoder.h"
#include "decode/PanelRenderer.h"
#include "deskhubp/net/ClientNetLoop.h"
#include "deskhubp/net/UdpSocket.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/diag/LogFile.h"

#include "deskhub/control/ClockOffset.h"
#include "deskhub/diag/ClientDiag.h"
#include "deskhub/session/ClientPump.h"
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

    std::unique_ptr<IVideoDecoder> decoder;
    std::atomic<uint32_t> decW{0}, decH{0}, decFps{0};

    std::atomic<int64_t> lastE2eUs{-1};
    deskhub::ClockOffset clockOffset;

    std::atomic<uint32_t> stRendered{0};

    deskhub::diag::ClientDiag diag{deskhub::diag::ClientDiagCaps{
        true, false}};

    constexpr size_t kMaxQueuedFrames = 3;
    std::mutex decQueueMutex;
    std::condition_variable decQueueCv;
    std::deque<deskhub::Reassembler::Frame> decQueue;
    std::atomic<bool> decodeThreadStop{false};
    std::atomic<bool> decodeFailedFlag{false};
    std::atomic<bool> queueOverflowFlag{false};
    std::atomic<bool> negotiated{false};

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
            deskhub::Reassembler::Frame f;
            {
                std::unique_lock<std::mutex> lk(decQueueMutex);
                decQueueCv.wait(lk, [&] { return decodeThreadStop.load() || !decQueue.empty(); });
                if (decQueue.empty()) {
                    if (decodeThreadStop.load()) break;
                    continue;
                }
                f = std::move(decQueue.front());
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
            const uint64_t t0 = NowUs();
            const bool ok = decoder->Decode(f.nal.data(), f.nal.size(), f.timestampUs);
            const uint32_t decMs = uint32_t((NowUs() - t0) / 1000);
            diag.decMs.Add(decMs);
            if (!ok) decodeFailedFlag.store(true, std::memory_order_release);
        }
        CoUninitialize();
    });

    bool closedNotified = false;

    deskhub::ClientPumpCallbacks cb;
    cb.send = [&](std::span<const uint8_t> d) { sock.SendTo(server, d.data(), d.size()); };
    cb.onFrame = [&](deskhub::Reassembler::Frame&& f) {
        {
            std::lock_guard<std::mutex> lk(decQueueMutex);
            if (decQueue.size() >= kMaxQueuedFrames) {
                decQueue.pop_front();
                queueOverflowFlag.store(true, std::memory_order_release);
                diag.dqDrop.Add();
            }
            decQueue.push_back(std::move(f));
        }
        decQueueCv.notify_one();
    };
    cb.onParams = [&](const deskhub::NegotiatedParams& np, bool) {
        negotiated.store(true, std::memory_order_release);
        decW.store(np.width, std::memory_order_relaxed);
        decH.store(np.height, std::memory_order_relaxed);
        decFps.store(np.fps ? np.fps : 60, std::memory_order_relaxed);
        if (sizeCb) sizeCb(np.width, np.height, user);
    };
    cb.onEnded = [&](const char* reason) {
        closedNotified = true;
        if (closedCb) closedCb(reason ? reason : "disconnected", user);
        quit.store(true);
    };
    cb.takeRenderedCount = [&] { return stRendered.exchange(0, std::memory_order_relaxed); };
    cb.latencyUs = [&] { return lastE2eUs.load(); };
    cb.onStatus = [&](const char* compact) {
        if (negotiated.load(std::memory_order_acquire) && statsCb) statsCb(compact, user);
    };
    cb.localTime = [] { return deskhubp::LocalTimeHms(); };
    cb.log = [](bool, const char* line) { std::printf("%s\n", line); };

    deskhub::ClientPump pump(std::move(cb), diag);

    deskhub::ClientPumpConfig pcfg;
    pcfg.clientId = deskhubp::MakeClientId(sourceId);
    pcfg.maxWidth = uint16_t(GetSystemMetrics(SM_CXVIRTUALSCREEN));
    pcfg.maxHeight = uint16_t(GetSystemMetrics(SM_CYVIRTUALSCREEN));
    pcfg.sourceId = sourceId;
    pcfg.desiredFps = 60;
    pcfg.sendNacks = true;
    pcfg.statusSeparator = " \xC2\xB7 ";
    pump.Start(pcfg, NowUs());

    uint8_t buf[deskhub::kMaxDatagram];

    while (!quit.load() && !failed.load()) {
        NetAddr from;
        const int n = sock.RecvFrom(buf, sizeof(buf), from);
        const uint64_t now = NowUs();
        if (n < 0) {
            failReason.store("socket error");
            failed.store(true);
            break;
        }
        if (n > 0) pump.OnDatagram(std::span<const uint8_t>(buf, size_t(n)), now);

        pump.PollFrames(now);
        if (decodeFailedFlag.exchange(false, std::memory_order_acq_rel))
            pump.RequestKeyframe("dec_fail", now);
        if (queueOverflowFlag.exchange(false, std::memory_order_acq_rel))
            pump.RequestKeyframe("q_overflow", now);

        pump.PlanNacks(now);

        {
            std::vector<deskhub::InputEvent> batch;
            {
                std::lock_guard<std::mutex> lk(inputMutex);
                batch.swap(inputQueue);
            }
            for (const auto& e : batch) pump.QueueInput(e);
        }

        pump.SetFocused(true);
        if (!pump.Tick(now)) break;

        pump.CountLoopBusy(now, NowUs());
    }

    decodeThreadStop.store(true);
    decQueueCv.notify_one();
    decodeThread.join();

    pump.SendBye();
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
