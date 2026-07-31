#include "ClientLoop.h"

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <memory>

#include "deskhubp/diag/Log.h"
#include "deskhubp/net/ClientNetLoop.h"
#include "deskhubp/system/Clock.h"
#include "deskhubp/diag/LogFile.h"

#include "deskhub/input/KeyMap.h"
#include "deskhub/session/ClientPump.h"
#include "deskhub/protocol/Wire.h"

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

void ClientLoop::QueueKeyTap(int32_t vk, int32_t scan) {
    input_.KeyTap(vk, scan, NowUs());
}

void ClientLoop::QueueMouseMoveAbs(int32_t nx, int32_t ny) {
    input_.MouseMoveAbsolute(nx, ny, NowUs());
}

void ClientLoop::QueueKeyChord(int32_t modVk, int32_t modScan, int32_t vk, int32_t scan) {
    input_.KeyChord(modVk, modScan, vk, scan, NowUs());
}

void ClientLoop::QueueMouseMoveRel(int32_t dx, int32_t dy) {
    input_.MouseMoveRelative(dx, dy, NowUs());
}

void ClientLoop::QueueMouseButton(int32_t button, bool down) {
    input_.MouseButtonEvent(button, down, NowUs());
}

void ClientLoop::QueueCharTap(uint32_t codepoint) {
    input_.CharTap(codepoint, NowUs());
}

bool ClientLoop::Start(const NetAddr& server, uint8_t sourceId, uint32_t screenW,
    uint32_t screenH) {
    server_ = server;
    sourceId_ = sourceId;
    screenW_ = screenW;
    screenH_ = screenH;
    if (!sock_.Open(0)) {
        LOGE("[Client] Failed to open socket.");
        return false;
    }
    sock_.SetRecvTimeout(10);

    quit_.store(false);
    finished_.store(false);
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
    winCv_.notify_all();
    if (netThread_.joinable()) netThread_.join();
    if (decodeThread_.joinable()) decodeThread_.join();
    sock_.Close();
}

void ClientLoop::SetWindow(ANativeWindow* window) {
    std::unique_lock<std::mutex> lk(winMutex_);
    window_ = window;
    ++winGen_;
    winCv_.notify_all();
    decCv_.notify_all();
    winAckCv_.wait(lk, [this] { return winAckGen_ == winGen_ || decodeExited_; });
}

void ClientLoop::DecodeThread() {
    MediaCodecDecoder decoder;
    clockOffset_.Reset();

    for (;;) {
        {
            std::unique_lock<std::mutex> lk(winMutex_);
            if (winAckGen_ != winGen_) {
                const uint64_t gen = winGen_;
                lk.unlock();
                decoder.Shutdown();
                lk.lock();
                winAckGen_ = gen;
                winAckCv_.notify_all();
                rebuildDecoder_.store(false);
                decodeFailed_.store(true, std::memory_order_release);
            }
        }

        if (quit_.load()) break;

        deskhub::Reassembler::Frame f;
        {
            std::unique_lock<std::mutex> lk(decMutex_);
            decCv_.wait_for(lk, std::chrono::milliseconds(20), [this] {
                return quit_.load() || !decQueue_.empty();
            });
            if (decQueue_.empty()) continue;
            f = std::move(decQueue_.front());
            decQueue_.pop_front();
        }

        if (rebuildDecoder_.exchange(false) && decoder.IsOpen()) decoder.Shutdown();

        if (!decoder.IsOpen()) {
            const uint32_t w = negW_.load(), h = negH_.load();
            if (!w || !h) continue;

            ANativeWindow* win = nullptr;
            {
                std::lock_guard<std::mutex> lk(winMutex_);
                win = window_;
            }
            if (!win) continue;

            if (!decoder.Init(win, int(w), int(h))) {
                decodeFailed_.store(true, std::memory_order_release);
                continue;
            }
        }

        const uint64_t t0 = NowUs();
        const bool ok = decoder.Decode(f.nal.data(), f.nal.size(), f.timestampUs);
        const uint64_t decMs = (NowUs() - t0) / 1000;
        diag_.decMs.Add(uint32_t(decMs));
        if (decMs > 20)
            LOGW("[Client] decode+render took %" PRIu64 " ms for one frame", decMs);

        if (!ok) {
            decodeFailed_.store(true, std::memory_order_release);
            decoder.Shutdown();
            continue;
        }

        if (const uint32_t n = decoder.TakeRenderedCount())
            stRendered_.fetch_add(n, std::memory_order_relaxed);

        if (const uint32_t n = decoder.TakeCongestionDrops()) {
            diag_.dispDrop.Add(n);
            displayCongested_.store(true, std::memory_order_release);
        }

        if (const uint64_t pts = decoder.lastRenderedPtsUs()) {
            clockOffset_.AddSample(pts, NowUs());
            lastE2eUs_.store(
                clockOffset_.LatencyUs(diag_.minRttUs.value() / 2));
        }
    }

    decoder.Shutdown();
    {
        std::lock_guard<std::mutex> lk(winMutex_);
        decodeExited_ = true;
        winAckGen_ = winGen_;
    }
    winAckCv_.notify_all();
}

void ClientLoop::NetThread() {
    deskhub::ClientPumpCallbacks cb;
    cb.send = [this](std::span<const uint8_t> d) {
        sock_.SendTo(server_, d.data(), d.size());
    };
    cb.onFrame = [this](deskhub::Reassembler::Frame&& f) {
        {
            std::lock_guard<std::mutex> lk(decMutex_);
            if (decQueue_.size() >= kMaxQueuedFrames) {
                decQueue_.pop_front();
                queueOverflow_.store(true, std::memory_order_release);
                diag_.dqDrop.Add();
            }
            decQueue_.push_back(std::move(f));
        }
        decCv_.notify_one();
    };
    cb.onParams = [this](const deskhub::NegotiatedParams& np, bool reconfigured) {
        negW_.store(np.width);
        negH_.store(np.height);
        if (reconfigured) rebuildDecoder_.store(true);
    };
    cb.onEnded = [this](const char* reason) {
        {
            std::lock_guard<std::mutex> lk(textMutex_);
            endReason_ = reason;
        }
        quit_.store(true);
    };
    cb.takeRenderedCount = [this] {
        return stRendered_.exchange(0, std::memory_order_relaxed);
    };
    cb.latencyUs = [this] { return lastE2eUs_.load(); };
    cb.onStatus = [this](const char* compact) {
        std::lock_guard<std::mutex> lk(textMutex_);
        statusLine_ = compact;
    };
    cb.localTime = [] { return deskhubp::LocalTimeHms(); };
    cb.log = [](bool warn, const char* line) {
        if (warn)
            LOGW("%s", line);
        else
            LOGI("%s", line);
    };

    deskhub::ClientPump pump(std::move(cb), diag_);

    deskhub::ClientPumpConfig pcfg;
    pcfg.clientId = deskhubp::MakeClientId(sourceId_);
    pcfg.maxWidth = uint16_t(screenW_);
    pcfg.maxHeight = uint16_t(screenH_);
    pcfg.sourceId = sourceId_;
    pcfg.desiredFps = 60;
    pcfg.sendNacks = true;
    pcfg.logLossRuns = true;
    pump.Start(pcfg, NowUs());

    std::vector<deskhub::InputEvent> inputBatch;

    deskhubp::ClientNetLoopHooks hooks;
    hooks.stopped = [this] { return quit_.load(); };
    hooks.afterFrames = [this](deskhub::ClientPump& p, uint64_t now) {
        if (decodeFailed_.exchange(false, std::memory_order_acq_rel))
            p.RequestKeyframe("dec_fail", now);
        if (displayCongested_.exchange(false, std::memory_order_acq_rel))
            p.RequestKeyframe("display_congested", now);
        if (queueOverflow_.exchange(false, std::memory_order_acq_rel))
            p.RequestKeyframe("q_overflow", now);
    };
    hooks.beforeTick = [this, &inputBatch](deskhub::ClientPump& p, uint64_t now) {
        input_.Drain(now, inputBatch);
        for (const auto& e : inputBatch) p.QueueInput(e);
        if (input_.wantsFocus()) p.SetFocused(true);
    };
    hooks.onPhase = [this](bool streaming) {
        phase_.store(streaming ? Phase::Streaming : Phase::Connecting,
            std::memory_order_release);
    };
    hooks.onSocketError = [this] {
        LOGE("[Client] Socket error.");
        std::lock_guard<std::mutex> lk(textMutex_);
        endReason_ = "socket error";
    };

    deskhubp::RunClientNetLoop(sock_, pump, hooks);

    quit_.store(true);
    decCv_.notify_all();
    {
        std::lock_guard<std::mutex> lk(textMutex_);
        if (endReason_.empty()) endReason_ = "stopped";
    }
    phase_.store(Phase::Ended, std::memory_order_release);
    finished_.store(true, std::memory_order_release);
    LOGI("[Client] Session ended.");
}
