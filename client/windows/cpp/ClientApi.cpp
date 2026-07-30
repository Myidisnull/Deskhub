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
//
// MỘT ĐIỂM KHÁC CÓ CHỦ Ý so với các viewer kia: nơi DỪNG đồng hồ e2e. Viewer này là
// viewer duy nhất tự sở hữu swapchain, nên nó là viewer duy nhất có một khoảng chờ
// trình bày đo được — xem khối chú thích ở onDecoded và docs/09 §End-to-end latency.
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include "DeskhubApi.h"

#include <windows.h>
#include <objbase.h> // CoInitializeEx cho thread dùng Media Foundation
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

#include "Diag.h" // DiagAtomicMax — bộ đếm max của cửa sổ chẩn đoán
#include "capture/GpuSelect.h"
#include "decode/IVideoDecoder.h"
#include "decode/PanelRenderer.h"
#include "net/UdpSocket.h"
#include "deskhubp/Clock.h"
#include "deskhubp/LogFile.h" // LocalTimeHms — đóng dấu giờ dòng mỗi giây

#include "deskhub/control/ClockOffset.h"
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
    std::vector<deskhub::InputEvent> inputQueue; // UI thread ghi, thread Recv rút

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

    // Ước lượng trễ e2e (deskhub/control/ClockOffset.h). minRttUs: Recv ghi, Decode
    // đọc. clockOffset: CHỈ thread Decode chạm (onDecoded chạy đồng bộ trong Decode)
    // — nó không tự khoá, và phải được bơm mẫu ở ĐÚNG MỘT điểm trong đường dẫn.
    std::atomic<uint32_t> minRttUs{0};
    std::atomic<int64_t> lastE2eUs{-1};
    deskhub::ClockOffset clockOffset;

    uint64_t stBytes = 0;
    std::atomic<uint32_t> stRendered{0};

    // --- Bộ đếm chẩn đoán cửa sổ 1s (docs/09) ---
    // Nhóm thường: CHỈ thread Recv chạm. Nhóm atomic: thread Decode ghi, Recv in.
    uint32_t dgAsmMsSum = 0, dgAsmMsMax = 0, dgAsmCount = 0; // t_asm: mảnh đầu → ghép xong
    uint32_t dgDqDrop = 0;                                   // frame vứt vì hàng đợi đầy
    uint32_t dgLoopBusyMaxMs = 0;                            // vòng Recv bận nhất
    std::atomic<uint32_t> dgDecMsSum{0}, dgDecMsMax{0}, dgDecCount{0};
    std::atomic<uint32_t> dgPresentMsSum{0}, dgPresentMsMax{0}, dgPresentCount{0};

    // Hàng đợi giữa Recv và Decode. Từ khi swapchain đặt MaximumFrameLatency(1)
    // (PanelRenderer.cpp), Present chặn thread Decode tới cả một nhịp quét màn hình —
    // nên hàng đợi này là chỗ HẤP THU DAO ĐỘNG giữa nhịp 60 fps của host và nhịp quét
    // của màn hình này. 3 frame đủ cho lệch pha thông thường; đầy hơn thế thì frame
    // CŨ NHẤT bị vứt (giữ lại chỉ làm hình trễ thêm) và `dq_drop` ở dòng evt=sum đếm
    // lại — đó là con số nói "màn hình quét chậm hơn tốc độ host gửi", không phải
    // "mạng kém".
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

    // Frame vừa giải mã xong -> vẽ + trình bày, rồi chốt e2e. Chạy ĐỒNG BỘ bên trong
    // decoder->Decode() (MfDecoder::Deliver gọi thẳng), tức trên thread Decode.
    //
    // ⚠ ĐỒNG HỒ e2e DỪNG TRƯỚC Present, KHÔNG PHẢI SAU (sửa 2026-07-30)
    //   Present chặn cho tới khi frame trước đó lên màn — tới cả một nhịp quét. Đó là
    //   thời gian frame NẰM CHỜ ở đây, không phải thời gian nó đi từ host về, nên đếm
    //   vào e2e là đổ lỗi cho đường truyền vì một điểm nghẽn của chính máy này. Nó cũng
    //   làm con số này không so được với client Apple: bên đó đồng hồ dừng lúc enqueue
    //   vào AVSampleBufferDisplayLayer, tức trước cả khi giải mã (VtDecoder.h,
    //   lastRenderedPtsUs). Dừng ở mốc "frame đã sẵn sàng trình bày" là điểm gần nhất
    //   với bên kia mà vẫn trung thực.
    //   Phần bị loại ra KHÔNG bị giấu: nó thành `present_ms` ở dòng evt=sum.
    auto onDecoded = [&](const DecodedFrame& df) {
        uint64_t readyUs = 0;
        if (!renderer.RenderNV12(df.texture, df.subresource, df.width, df.height, &readyUs))
            return;
        stRendered.fetch_add(1, std::memory_order_relaxed);

        if (readyUs) {
            const uint32_t pms = uint32_t((NowUs() - readyUs) / 1000);
            dgPresentMsSum.fetch_add(pms, std::memory_order_relaxed);
            dgPresentCount.fetch_add(1, std::memory_order_relaxed);
            DiagAtomicMax(dgPresentMsMax, pms);
        }

        // `pts` phải khác 0 y như bốn viewer kia: host chưa gắn mốc (hoặc gói hỏng)
        // thì pts=0 kéo sàn của bộ lọc min xuống một giá trị vô nghĩa, và mọi frame
        // sau đó bị báo trễ theo.
        if (df.timestampUs) {
            // readyUs = 0 chỉ xảy ra nếu renderer đổi mà quên trả mốc — lùi về NowUs()
            // để phép đo tệ đi một chút chứ không biến mất.
            clockOffset.AddSample(df.timestampUs, readyUs ? readyUs : NowUs());
            // Cộng lại sàn mạng đo được (nửa RTT nhỏ nhất). RTT chưa về thì để 0:
            // thà hụt sàn còn hơn bịa ra nó.
            lastE2eUs.store(
                clockOffset.LatencyUs(minRttUs.load(std::memory_order_relaxed) / 2));
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
            // t_dec (docs/09). BAO GỒM cả onDecoded — vẽ + Present chạy đồng bộ bên
            // trong Decode(). Tách hai phần ra là việc của `present_ms`: dec_ms xấp xỉ
            // present_ms nghĩa là nghẽn ở khâu trình bày, còn dec_ms lớn hơn hẳn thì
            // chính MFT mới là chỗ chậm.
            deskhub::Reassembler::Frame& f = it.frame;
            const uint64_t t0 = NowUs();
            const bool ok = decoder->Decode(f.nal.data(), f.nal.size(), f.timestampUs);
            const uint32_t decMs = uint32_t((NowUs() - t0) / 1000);
            dgDecMsSum.fetch_add(decMs, std::memory_order_relaxed);
            dgDecCount.fetch_add(1, std::memory_order_relaxed);
            DiagAtomicMax(dgDecMsMax, decMs);
            if (!ok) decodeFailedFlag.store(true, std::memory_order_release);
        }
        CoUninitialize();
    });

    deskhub::ClientCallbacks cb;
    cb.send = [&](std::span<const uint8_t> d) { sock.SendTo(server, d.data(), d.size()); };
    cb.onReady = [&](const deskhub::NegotiatedParams& np) {
        // HelloAck::timebaseUs KHÔNG còn được dùng để ước lượng độ lệch đồng hồ nữa:
        // một mẫu duy nhất, lấy từ gói đầu tiên của phiên, là mẫu tệ nhất có thể lấy
        // (xem deskhub/control/ClockOffset.h). Bộ lọc min ở onDecoded thay nó.
        negotiated = true;
        decW.store(np.width, std::memory_order_relaxed);
        decH.store(np.height, std::memory_order_relaxed);
        decFps.store(np.fps ? np.fps : 60, std::memory_order_relaxed);
        if (sizeCb) sizeCb(np.width, np.height, user);
    };
    cb.onReconfig = [&](const deskhub::NegotiatedParams& np) {
        // fps mới phải tới được Reassembler, không chỉ để hiển thị: nó là hạn chờ
        // trước khi khai tử một frame thiếu mảnh. Giữ hạn của fps cũ khi host đã hạ
        // fps = bỏ frame LÀNH rồi xin IDR, đúng lúc đường truyền yếu nhất
        // (xem deskhub::Reconfig::fps).
        if (reasm) reasm->SetFps(np.fps);
        decFps.store(np.fps ? np.fps : decFps.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
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
    // Cỡ MÀN HÌNH máy này (pixel). Host co luồng cho vừa thay vì gửi nguyên độ phân
    // giải nguồn — xem deskhub::Hello::maxWidth.
    //
    // SM_CX/CYVIRTUALSCREEN chứ không phải SM_CXSCREEN: người dùng nhiều màn hình gần
    // như chắc chắn xem ở màn to nhất, và khai theo màn CHÍNH sẽ khoá luồng ở độ phân
    // giải của một màn họ không nhìn. Hộp bao ảo là trần đúng cho mọi bố trí.
    hello.maxWidth = uint16_t(GetSystemMetrics(SM_CXVIRTUALSCREEN));
    hello.maxHeight = uint16_t(GetSystemMetrics(SM_CYVIRTUALSCREEN));
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
                        // Khám nghiệm từng frame bị khai tử. `pos` là thứ phân biệt mất
                        // gói theo chùm (tail) với mất lẻ tẻ — xem docs/06 §5.
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

        // Gom mọi đường dẫn tới "xin IDR" về một chỗ, kèm lý do. Gọi lặp là vô hại:
        // chỉ log ở lần chuyển từ "không treo" sang "đang treo".
        auto requestKf = [&](const char* reason) {
            if (!kfReqUs) {
                kfReqUs = now;
                std::printf("[DIAG] evt=kf_req reason=%s\n", reason);
            }
            session.RequestKeyframe();
        };

        if (reasm) {
            while (auto f = reasm->PopReady(now)) {
                if (f->idr) {
                    session.CancelKeyframeRequest();
                    if (kfReqUs) {
                        std::printf("[DIAG] evt=idr_rx bytes=%zu after_ms=%" PRIu64 "\n",
                            f->nal.size(), (now - kfReqUs) / 1000);
                        kfReqUs = 0;
                    }
                }
                // t_asm: mảnh đầu tiên tới → frame ghép xong và rời Reassembler.
                if (f->firstSeenUs) {
                    const uint32_t asmMs = uint32_t((now - f->firstSeenUs) / 1000);
                    dgAsmMsSum += asmMs;
                    ++dgAsmCount;
                    if (asmMs > dgAsmMsMax) dgAsmMsMax = asmMs;
                }
                {
                    std::lock_guard<std::mutex> lk(decQueueMutex);
                    if (decQueue.size() >= kMaxQueuedFrames) {
                        decQueue.pop_front();
                        queueOverflowFlag.store(true, std::memory_order_release);
                        ++dgDqDrop;
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

        // Vét input do app đẩy vào -> ClientSession đánh seq, Tick gửi.
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

            // Dòng trạng thái 1s, đối ứng [Client] của các client kia — đây là bản
            // duy nhất đi vào file log (statsCb chỉ vẽ lên thanh trên của cửa sổ).
            std::printf("[Client t=%s] %2.0f fps | %6.0f kbps | dropped %" PRIu64
                        " frame | lost %4.1f%% pkts | fec+%" PRIu64
                        " | RTT %.1f ms | e2e ~%.1f ms\n",
                deskhubp::LocalTimeHms().c_str(), w.fps, w.kbps, w.framesDropped, w.lossPct, w.packetsRecovered,
                session.lastRttUs() / 1000.0, e2e >= 0 ? e2e / 1000.0 : 0.0);

            // Dòng chẩn đoán 1s (docs/09) — đọc-và-reset mọi bộ đếm cửa sổ.
            {
                const uint32_t dc = dgDecCount.exchange(0, std::memory_order_relaxed);
                const uint32_t ds = dgDecMsSum.exchange(0, std::memory_order_relaxed);
                const uint32_t dm = dgDecMsMax.exchange(0, std::memory_order_relaxed);
                const uint32_t pc = dgPresentCount.exchange(0, std::memory_order_relaxed);
                const uint32_t ps = dgPresentMsSum.exchange(0, std::memory_order_relaxed);
                const uint32_t pm = dgPresentMsMax.exchange(0, std::memory_order_relaxed);
                std::printf(
                    "[DIAG] evt=sum asm_ms=%.1f/%u dec_ms=%.1f/%u present_ms=%.1f/%u"
                    " dq_drop=%u late=%" PRIu64 " late_ms_avg=%.0f late_ms_max=%" PRIu64
                    " gap_ms_max=%u loop_busy_ms_max=%u min_rtt_ms=%.1f e2e_ms=%.1f\n",
                    dgAsmCount ? double(dgAsmMsSum) / dgAsmCount : 0.0, dgAsmMsMax,
                    dc ? double(ds) / dc : 0.0, dm,
                    pc ? double(ps) / pc : 0.0, pm,
                    dgDqDrop, w.latePackets, w.lateMsAvg, w.lateMsMax,
                    reasm ? reasm->TakeMaxGapMs() : 0, dgLoopBusyMaxMs,
                    minRttUs.load(std::memory_order_relaxed) / 1000.0, e2e / 1000.0);
                dgAsmMsSum = dgAsmMsMax = dgAsmCount = 0;
                dgDqDrop = 0;
                dgLoopBusyMaxMs = 0;
            }

            stBytes = 0;
        }

        // Vòng này bận bao lâu (không tính lúc chờ recvfrom). Thread Recv nghẽn thì
        // buffer UDP của kernel gánh — tràn là mất gói THẬT, ngay tại máy này.
        const uint32_t busyMs = uint32_t((NowUs() - now) / 1000);
        if (busyMs > dgLoopBusyMaxMs) dgLoopBusyMaxMs = busyMs;
        if (busyMs > 50) std::printf("[DIAG] evt=recv_stall busy_ms=%u\n", busyMs);
    }

    decodeThreadStop.store(true);
    decQueueCv.notify_one();
    decodeThread.join();

    session.SendBye();
    quit.store(true);

    // MỌI đường chết không do dh_client_stop đều phải báo về app, kể cả đường native
    // (decoder hỏng, lỗi socket, phiên Dead) — thiếu là cửa sổ xem đứng hình vĩnh viễn.
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
    h->userStop.store(true); // dừng chủ động: đừng bắn closedCb ngược vào app đang thoát
    h->quit.store(true);
    if (h->thread.joinable()) h->thread.join();
    delete h;
}
