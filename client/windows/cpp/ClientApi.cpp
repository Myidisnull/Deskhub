// =============================================================================
// ClientApi.cpp — cài đặt C API vai CLIENT/VIEWER (xem DeskhubApi.h §VAI CLIENT).
//
// Đây là bản HEADLESS, MỘT NGUỒN của vòng xem: thread Recv + thread Decode, hàng
// đợi frame, ước lượng e2e, NACK, feedback — không tự sở hữu cửa sổ nào. Render đi
// qua PanelRenderer vào cửa sổ con do app cấp, input đến qua C API. Người gọi duy
// nhất là app win32 (Viewer.cpp) qua dh_client_start_hwnd.
//
// Đọc ClientLoop.cpp (StreamRecvLoop) để hiểu VÌ SAO tách thread Decode khỏi Recv và
// cách ước lượng e2e — phần đó ở đây là bản rút gọn nguyên vẹn logic.
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include "DeskhubApi.h"

#include <windows.h>
#include <objbase.h> // CoInitializeEx cho thread dùng Media Foundation
#include <atomic>
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
#include "net/UdpSocket.h"
#include "deskhubp/Clock.h"

#include "deskhub/control/LinkStats.h"
#include "deskhub/session/ClientSession.h"
#include "deskhub/transport/Reassembler.h"

// Handle mờ: state của một phiên xem + hai thread của nó.
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
    std::atomic<bool> userStop{false};            // dh_client_stop chủ động dừng
    std::atomic<const char*> failReason{nullptr}; // chuỗi TĨNH mô tả đường chết

    std::mutex inputMutex;
    std::vector<deskhub::InputEvent> inputQueue; // C# ghi, thread Recv rút

    std::thread thread;

    void Run();
    void PushInput(const deskhub::InputEvent& e) {
        std::lock_guard<std::mutex> lk(inputMutex);
        inputQueue.push_back(e);
    }
};

// Thân thread Recv (kèm thread Decode bên trong). Port rút gọn của StreamRecvLoop.
void DhClientHandle::Run() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED); // MF/D3D11VA cần COM trên thread này

    std::unique_ptr<deskhub::Reassembler> reasm;
    std::unique_ptr<IVideoDecoder> decoder;
    std::atomic<uint32_t> decW{0}, decH{0}, decFps{0};

    // Ước lượng trễ e2e — ghi ở Recv, đọc ở Decode (trong onDecoded).
    std::atomic<int64_t> ackDeltaUs{0};
    std::atomic<uint32_t> minRttUs{0};
    std::atomic<int64_t> lastE2eUs{-1};

    uint64_t stBytes = 0;
    std::atomic<uint32_t> stRendered{0};

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
        if (!renderer.RenderNV12(df.texture, df.subresource, df.width, df.height)) return;
        stRendered.fetch_add(1, std::memory_order_relaxed);
        const uint32_t rtt = minRttUs.load(std::memory_order_relaxed);
        if (rtt) {
            const int64_t offset = ackDeltaUs.load(std::memory_order_relaxed) - int64_t(rtt) / 2;
            lastE2eUs.store(int64_t(NowUs()) - offset - int64_t(df.timestampUs));
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
            const bool ok = decoder->Decode(f.nal.data(), f.nal.size(), f.timestampUs);
            if (!ok) decodeFailedFlag.store(true, std::memory_order_release);
        }
        CoUninitialize();
    });

    deskhub::ClientCallbacks cb;
    cb.send = [&](std::span<const uint8_t> d) { sock.SendTo(server, d.data(), d.size()); };
    cb.onReady = [&](const deskhub::NegotiatedParams& np) {
        ackDeltaUs.store(int64_t(NowUs()) - int64_t(np.timebaseUs), std::memory_order_relaxed);
        negotiated = true;
        decW.store(np.width, std::memory_order_relaxed);
        decH.store(np.height, std::memory_order_relaxed);
        decFps.store(np.fps ? np.fps : 60, std::memory_order_relaxed);
        if (sizeCb) sizeCb(np.width, np.height, user);
    };
    cb.onReconfig = [&](const deskhub::NegotiatedParams& np) {
        decW.store(np.width, std::memory_order_relaxed);
        decH.store(np.height, std::memory_order_relaxed);
        if (sizeCb) sizeCb(np.width, np.height, user);
        // Không dựng lại decoder/renderer: MfDecoder tự đàm phán lại khi gặp SPS mới,
        // PanelRenderer tự ResizeBuffers theo cỡ frame.
    };
    cb.onRtt = [&](uint32_t rttUs) {
        uint32_t cur = minRttUs.load(std::memory_order_relaxed);
        while ((!cur || rttUs < cur) &&
               !minRttUs.compare_exchange_weak(cur, rttUs, std::memory_order_relaxed)) {}
    };
    bool closedNotified = false; // chỉ đọc/ghi trên thread Run (session gọi callback tại đây)
    cb.onDisconnect = [&](const char* reason) {
        closedNotified = true;
        if (closedCb) closedCb(reason ? reason : "disconnected", user);
        quit.store(true);
    };

    deskhub::ClientSession session(cb);

    deskhub::Hello hello;
    hello.clientId = uint32_t(NowUs()) ^ GetCurrentProcessId() ^ (uint32_t(sourceId) << 24);
    hello.codecMask = deskhub::kCodecMaskH264;
    hello.maxWidth = uint16_t(GetSystemMetrics(SM_CXSCREEN));
    hello.maxHeight = uint16_t(GetSystemMetrics(SM_CYSCREEN));
    hello.desiredFps = 60;
    hello.features = 0;
    hello.sourceId = sourceId;
    session.Start(hello, NowUs());

    uint8_t buf[deskhub::kMaxDatagram];
    deskhub::LinkStats linkStats(NowUs());
    uint64_t kfReqUs = 0;

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

        auto requestKf = [&](const char*) {
            if (!kfReqUs) kfReqUs = now;
            session.RequestKeyframe();
        };

        if (reasm) {
            while (auto f = reasm->PopReady(now)) {
                if (f->idr) {
                    session.CancelKeyframeRequest();
                    kfReqUs = 0;
                }
                {
                    std::lock_guard<std::mutex> lk(decQueueMutex);
                    if (decQueue.size() >= kMaxQueuedFrames) {
                        decQueue.pop_front();
                        queueOverflowFlag.store(true, std::memory_order_release);
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
            const size_t nn = reasm->PlanNack(now, minRttUs.load(std::memory_order_relaxed),
                nackFrame, nackIdx);
            if (nn) session.SendNack(nackFrame, std::span<const uint16_t>(nackIdx, nn));
        }

        // Vét input do C# đẩy vào -> ClientSession đánh seq, Tick gửi.
        {
            std::vector<deskhub::InputEvent> batch;
            {
                std::lock_guard<std::mutex> lk(inputMutex);
                batch.swap(inputQueue);
            }
            for (const auto& e : batch) session.QueueInput(e);
        }

        session.SetFocused(true); // panel đang xem = luôn "focus" nguồn này
        session.Tick(now);
        if (session.state() == deskhub::ClientSession::State::Dead) break;

        if (linkStats.Due(now)) {
            const auto st = reasm ? reasm->stats() : deskhub::Reassembler::Stats{};
            const uint32_t rendered = stRendered.exchange(0, std::memory_order_relaxed);
            const deskhub::LinkWindow w = linkStats.Close(st, stBytes, rendered, now);
            const int64_t e2e = lastE2eUs.load();
            session.SendFeedback(deskhub::MakeFeedback(w, session.lastRttUs()));

            if (negotiated && statsCb) {
                char line[192];
                std::snprintf(line, sizeof(line),
                    "%.0f fps \xC2\xB7 %.1f Mbps \xC2\xB7 loss %.1f%% \xC2\xB7 RTT %.0f ms \xC2\xB7 e2e %.0f ms",
                    w.fps, w.kbps / 1000.0, w.lossPct, session.lastRttUs() / 1000.0,
                    e2e >= 0 ? e2e / 1000.0 : 0.0);
                statsCb(line, user);
            }
            stBytes = 0;
        }
    }

    decodeThreadStop.store(true);
    decQueueCv.notify_one();
    decodeThread.join();

    session.SendBye();
    quit.store(true);

    // MỌI đường chết không do dh_client_stop đều phải báo về C#, kể cả đường native
    // (decoder hỏng, lỗi socket, phiên Dead) — thiếu là ViewerPage đứng hình vĩnh viễn.
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
    e.absolute = 0; // host bơm MOUSEEVENTF_MOVE (delta thô) — đường game đọc được
    return e;
}

} // namespace

namespace {

// Thân của dh_client_start_hwnd, tách riêng để giữ cấu trúc validate-rồi-chạy gọn.
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
    // Tạo swapchain NGAY trên thread gọi (UI) — cần trước khi cửa sổ con hiện.
    // Cỡ 1280x720 chỉ là tạm — frame đầu sẽ ResizeBuffers về cỡ thật.
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

} // namespace

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
    h->userStop.store(true); // dừng chủ động: đừng bắn closedCb ngược vào C# đang thoát
    h->quit.store(true);
    if (h->thread.joinable()) h->thread.join();
    delete h;
}

