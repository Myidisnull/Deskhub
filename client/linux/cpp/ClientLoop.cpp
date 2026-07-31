#include "ClientLoop.h"

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <memory>
#include <utility>

#include "deskhubp/Log.h"
#include "decode/AvDecoder.h"
#include "deskhubp/Clock.h"
#include "deskhubp/LogFile.h"
#include "render/VideoSink.h"

#include "deskhub/control/LinkStats.h"
#include "deskhub/protocol/Wire.h"
#include "deskhub/session/ClientSession.h"

ClientLoop::~ClientLoop() {
    Stop();
}

std::string ClientLoop::StatusLine() {
    std::lock_guard<std::mutex> lk(textMutex_);
    return statusLine_;
}

std::string ClientLoop::EndReason() {
    std::lock_guard<std::mutex> lk(textMutex_);
    return endReason_;
}

void ClientLoop::PushLocked(const deskhub::InputEvent& e) {
    inputQueue_.push_back(e);
    wantFocus_.store(true, std::memory_order_release);
}

void ClientLoop::QueueKey(int32_t vk, int32_t scan, bool down) {
    deskhub::InputEvent e;
    e.type = deskhub::InputType::Key;
    e.timestampUs = NowUs();
    e.a = vk;
    e.b = scan;
    e.state = down ? 1 : 0;
    std::lock_guard<std::mutex> lk(inputMutex_);
    if (down)
        keysDown_[vk] = scan;
    else
        keysDown_.erase(vk);
    PushLocked(e);
}

void ClientLoop::ReleaseAllInput() {
    const uint64_t now = NowUs();
    std::lock_guard<std::mutex> lk(inputMutex_);
    for (const auto& [vk, scan] : keysDown_) {
        deskhub::InputEvent e;
        e.type = deskhub::InputType::Key;
        e.timestampUs = now;
        e.a = vk;
        e.b = scan;
        e.state = 0;
        PushLocked(e);
    }
    keysDown_.clear();
    for (int32_t btn : buttonsDown_) {
        deskhub::InputEvent e;
        e.type = deskhub::InputType::MouseButton;
        e.timestampUs = now;
        e.a = btn;
        e.state = 0;
        PushLocked(e);
    }
    buttonsDown_.clear();
}

void ClientLoop::QueueMouseMoveAbs(int32_t nx, int32_t ny) {
    auto clamp = [](int32_t v) { return v < 0 ? 0 : (v > 65535 ? 65535 : v); };
    deskhub::InputEvent e;
    e.type = deskhub::InputType::MouseMove;
    e.timestampUs = NowUs();
    e.a = clamp(nx);
    e.b = clamp(ny);
    e.absolute = 1;
    std::lock_guard<std::mutex> lk(inputMutex_);
    PushLocked(e);
}

void ClientLoop::QueueMouseMoveRel(int32_t dx, int32_t dy) {
    if (dx == 0 && dy == 0) return;
    deskhub::InputEvent e;
    e.type = deskhub::InputType::MouseMove;
    e.timestampUs = NowUs();
    e.a = dx;
    e.b = dy;
    e.absolute = 0;
    std::lock_guard<std::mutex> lk(inputMutex_);
    PushLocked(e);
}

void ClientLoop::QueueMouseButton(int32_t button, bool down) {
    deskhub::InputEvent e;
    e.type = deskhub::InputType::MouseButton;
    e.timestampUs = NowUs();
    e.a = button;
    e.state = down ? 1 : 0;
    std::lock_guard<std::mutex> lk(inputMutex_);
    if (down)
        buttonsDown_.insert(button);
    else
        buttonsDown_.erase(button);
    PushLocked(e);
}

void ClientLoop::QueueMouseWheel(int32_t delta) {
    if (delta == 0) return;
    deskhub::InputEvent e;
    e.type = deskhub::InputType::MouseWheel;
    e.timestampUs = NowUs();
    e.a = 0;
    e.b = delta;
    std::lock_guard<std::mutex> lk(inputMutex_);
    PushLocked(e);
}

bool ClientLoop::Start(const NetAddr& server, uint8_t sourceId, VideoSink* sink,
    uint32_t screenW, uint32_t screenH) {
    server_ = server;
    sourceId_ = sourceId;
    sink_ = sink;
    screenW_ = screenW;
    screenH_ = screenH;
    if (!sock_.Open(0)) {
        LOGE("[Client] Failed to open socket.");
        return false;
    }
    sock_.SetRecvTimeout(10);

    quit_.store(false);
    phase_.store(Phase::Connecting, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lk(textMutex_);
        statusLine_.clear();
        endReason_.clear();
    }
    decodeThread_ = std::thread([this] { DecodeThread(); });
    netThread_ = std::thread([this] { NetThread(); });
    LOGI("[Client] Connecting to %s (source %u) ...", server_.ToString().c_str(), sourceId_);
    return true;
}

void ClientLoop::Stop() {
    quit_.store(true);
    decCv_.notify_all();
    if (netThread_.joinable()) netThread_.join();
    if (decodeThread_.joinable()) decodeThread_.join();
    sock_.Close();
}

void ClientLoop::DecodeThread() {
    AvDecoder decoder;
    clockOffset_.Reset();

    while (!quit_.load()) {
        deskhub::Reassembler::Frame f;
        {
            std::unique_lock<std::mutex> lk(decMutex_);
            decCv_.wait_for(lk, std::chrono::milliseconds(20),
                [this] { return quit_.load() || !decQueue_.empty(); });
            if (decQueue_.empty()) continue;
            f = std::move(decQueue_.front());
            decQueue_.pop_front();
        }

        if (rebuildDecoder_.exchange(false) && decoder.IsOpen()) decoder.Shutdown();

        if (!decoder.IsOpen()) {
            const uint32_t w = negW_.load(), h = negH_.load();
            if (!w || !h) continue;
            if (!decoder.Init(sink_, int(w), int(h))) {
                decodeFailed_.store(true, std::memory_order_release);
                continue;
            }
        }

        const uint64_t t0 = NowUs();
        const bool ok = decoder.Decode(f.nal.data(), f.nal.size(), f.timestampUs);
        const uint64_t decMs = (NowUs() - t0) / 1000;
        diag_.decMs.Add(uint32_t(decMs));
        if (decMs > 20) LOGW("[Client] decode took %" PRIu64 " ms for one frame", decMs);

        if (!ok) {
            decodeFailed_.store(true, std::memory_order_release);
            decoder.Shutdown();
            continue;
        }

        if (const uint64_t pts = sink_ ? sink_->lastRenderedPtsUs() : 0) {
            clockOffset_.AddSample(pts, NowUs());
            lastE2eUs_.store(clockOffset_.LatencyUs(diag_.minRttUs.value() / 2),
                std::memory_order_relaxed);
        }
    }

    decoder.Shutdown();
}

void ClientLoop::NetThread() {
    std::unique_ptr<deskhub::Reassembler> reasm;

    deskhub::ClientCallbacks cb;
    cb.send = [this](std::span<const uint8_t> d) { sock_.SendTo(server_, d.data(), d.size()); };
    cb.onReady = [this](const deskhub::NegotiatedParams& np) {
        LOGI("[Client] Negotiation done: H264 %ux%u @%ufps, %.1f Mbps", np.width, np.height,
            np.fps, np.bitrateBps / 1e6);
        negW_.store(np.width);
        negH_.store(np.height);
    };
    cb.onReconfig = [this, &reasm](const deskhub::NegotiatedParams& np) {
        if (reasm) reasm->SetFps(np.fps);
        LOGI("[Client] Host reconfigured: %ux%u @%ufps, %.1f Mbps", np.width, np.height,
            np.fps, np.bitrateBps / 1e6);
        negW_.store(np.width);
        negH_.store(np.height);
        rebuildDecoder_.store(true);
    };
    cb.onRtt = [this](uint32_t rttUs) { diag_.minRttUs.Add(rttUs); };
    cb.onDisconnect = [this](const char* reason) {
        LOGI("[Client] Disconnected: %s", reason);
        {
            std::lock_guard<std::mutex> lk(textMutex_);
            endReason_ = reason ? reason : "disconnected";
        }
        quit_.store(true);
    };
    deskhub::ClientSession session(cb);

    deskhub::Hello hello;
    hello.clientId = uint32_t(NowUs());
    hello.codecMask = deskhub::kCodecMaskH264;
    hello.maxWidth = uint16_t(screenW_);
    hello.maxHeight = uint16_t(screenH_);
    hello.desiredFps = 60;
    hello.features = 0;
    hello.sourceId = sourceId_;
    session.Start(hello, NowUs());

    uint8_t buf[deskhub::kMaxDatagram];
    uint64_t stBytes = 0;
    deskhub::LinkStats linkStats(NowUs());

    deskhub::diag::KeyframeRequestLog kfLog;
    char kfLine[deskhub::diag::KeyframeRequestLog::kBufBytes];

    while (!quit_.load()) {
        NetAddr from;
        const int n = sock_.RecvFrom(buf, sizeof(buf), from);
        const uint64_t now = NowUs();
        if (n < 0) {
            LOGE("[Client] Socket error.");
            std::lock_guard<std::mutex> lk(textMutex_);
            endReason_ = "socket error";
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
                            char line[deskhub::diag::ClientDiag::kFrameDropBufBytes];
                            LOGW("%s", deskhub::diag::ClientDiag::FormatFrameDrop(line, sizeof(line), d));
                        };
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
                LOGI("%s", l);
            session.RequestKeyframe();
        };

        if (reasm) {
            while (auto f = reasm->PopReady(now)) {
                if (f->idr) {
                    session.CancelKeyframeRequest();
                    if (const char* l = kfLog.Arrived(kfLine, sizeof(kfLine), now, f->nal.size()))
                        LOGI("%s", l);
                }
                if (f->firstSeenUs) diag_.asmMs.Add(uint32_t((now - f->firstSeenUs) / 1000));
                {
                    std::lock_guard<std::mutex> lk(decMutex_);
                    if (decQueue_.size() >= kMaxQueuedFrames) {
                        decQueue_.pop_front();
                        queueOverflow_.store(true, std::memory_order_release);
                        diag_.dqDrop.Add();
                    }
                    decQueue_.push_back(std::move(*f));
                }
                decCv_.notify_one();
            }
            if (reasm->TakeLossEvent())
                requestKf("loss");
            else if (reasm->WaitingForIdr())
                requestKf("wait_idr");
        }
        if (decodeFailed_.exchange(false, std::memory_order_acq_rel)) requestKf("dec_fail");
        if (queueOverflow_.exchange(false, std::memory_order_acq_rel)) requestKf("q_overflow");

        {
            std::vector<deskhub::InputEvent> batch;
            {
                std::lock_guard<std::mutex> lk(inputMutex_);
                batch.swap(inputQueue_);
            }
            for (const auto& e : batch) session.QueueInput(e);
        }
        if (wantFocus_.load(std::memory_order_acquire)) session.SetFocused(true);

        session.Tick(now);
        if (session.state() == deskhub::ClientSession::State::Dead) break;

        phase_.store(session.state() == deskhub::ClientSession::State::Streaming
                         ? Phase::Streaming
                         : Phase::Connecting,
            std::memory_order_release);

        if (linkStats.Due(now)) {
            const auto st = reasm ? reasm->stats() : deskhub::Reassembler::Stats{};
            const uint32_t rendered = sink_ ? sink_->TakeRenderedCount() : 0;
            const deskhub::LinkWindow w = linkStats.Close(st, stBytes, rendered, now);

            const int64_t e2e = lastE2eUs_.load(std::memory_order_relaxed);

            const std::string hms = deskhubp::LocalTimeHms();
            char line[deskhub::diag::ClientDiag::kSumBufBytes];
            LOGI("%s", deskhub::diag::ClientDiag::FormatStatus(line, sizeof(line), hms.c_str(), w,
                           session.lastRttUs(), e2e));

            char ui[deskhub::diag::ClientDiag::kCompactBufBytes];
            deskhub::diag::ClientDiag::FormatCompact(ui, sizeof(ui), w, session.lastRttUs(), e2e);
            {
                std::lock_guard<std::mutex> lk(textMutex_);
                statusLine_ = ui;
            }

            session.SendFeedback(deskhub::MakeFeedback(w, session.lastRttUs()));

            LOGI("%s", diag_.FormatSum(line, sizeof(line), hms.c_str(), w,
                           reasm ? reasm->TakeMaxGapMs() : 0, e2e));

            stBytes = 0;
        }

        const uint32_t busyMs = uint32_t((NowUs() - now) / 1000);
        diag_.loopBusyMs.Add(busyMs);
        if (busyMs > 50) LOGW("[DIAG] evt=recv_stall busy_ms=%u", busyMs);
    }

    session.SendBye();
    quit_.store(true);
    decCv_.notify_all();
    {
        std::lock_guard<std::mutex> lk(textMutex_);
        if (endReason_.empty()) endReason_ = "stopped";
    }
    phase_.store(Phase::Ended, std::memory_order_release);
    LOGI("[Client] Session ended.");
}
