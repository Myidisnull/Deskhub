#pragma once
#include "deskhubp/diag/Log.h"
#include "deskhubp/system/Clock.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace deskhubp {

inline constexpr uint32_t kStopStallWarnMs = 500;
inline constexpr uint32_t kStopAnrMs = 2000;

inline uint32_t ElapsedMsSince(uint64_t t0Us) {
    const uint64_t dt = NowUs() - t0Us;
    return uint32_t(dt / 1000);
}

inline void LogStopPhase(const char* scope, const char* phase, uint64_t t0Us,
    uint32_t warnMs = kStopStallWarnMs, uint32_t anrMs = kStopAnrMs) {
    const uint32_t ms = ElapsedMsSince(t0Us);
    if (ms >= anrMs)
        LOGE("[DIAG][%s] evt=stop_anr phase=%s ms=%u state=done", scope, phase, ms);
    else if (ms >= warnMs)
        LOGW("[DIAG][%s] evt=stop_stall phase=%s ms=%u", scope, phase, ms);
    else
        LOGI("[DIAG][%s] evt=stop_ok phase=%s ms=%u", scope, phase, ms);
}

inline void LogStopPhaseNamed(const char* scope, const char* phase, const char* name,
    uint64_t t0Us, uint32_t warnMs = kStopStallWarnMs, uint32_t anrMs = kStopAnrMs) {
    const uint32_t ms = ElapsedMsSince(t0Us);
    if (ms >= anrMs)
        LOGE("[DIAG][%s] evt=stop_anr phase=%s name=%s ms=%u state=done", scope, phase, name,
            ms);
    else if (ms >= warnMs)
        LOGW("[DIAG][%s] evt=stop_stall phase=%s name=%s ms=%u", scope, phase, name, ms);
    else
        LOGI("[DIAG][%s] evt=stop_ok phase=%s name=%s ms=%u", scope, phase, name, ms);
}

inline bool TimedTryLock(std::unique_lock<std::mutex>& lk, uint32_t timeoutMs) {
    const uint64_t t0 = NowUs();
    while (!lk.try_lock()) {
        if (ElapsedMsSince(t0) >= timeoutMs) return false;
        SleepUs(1000);
    }
    return true;
}

class StopAnrWatch {
public:
    using SnapshotFn = std::function<void(uint32_t waitedMs)>;

    StopAnrWatch(const char* scope, const char* phase, SnapshotFn snapshot = {})
        : scope_(scope), phase_(phase), snapshot_(std::move(snapshot)) {
        thr_ = std::thread([this] { Run(); });
    }
    ~StopAnrWatch() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            done_ = true;
        }
        cv_.notify_all();
        if (thr_.joinable()) thr_.join();
    }
    StopAnrWatch(const StopAnrWatch&) = delete;
    StopAnrWatch& operator=(const StopAnrWatch&) = delete;

private:
    void Run() {
        std::unique_lock<std::mutex> lk(mu_);
        uint32_t waited = 0;
        bool logged = false;
        while (!done_) {
            if (cv_.wait_for(lk, std::chrono::milliseconds(50), [this] { return done_; }))
                return;
            waited += 50;
            if (!logged && waited >= kStopAnrMs) {
                lk.unlock();
                LOGE("[DIAG][%s] evt=stop_anr phase=%s ms=%u state=blocked", scope_, phase_,
                    waited);
                if (snapshot_) snapshot_(waited);
                lk.lock();
                logged = true;
            } else if (logged && waited % 5000u == 0u) {
                lk.unlock();
                LOGE("[DIAG][%s] evt=stop_anr phase=%s ms=%u state=still_blocked", scope_,
                    phase_, waited);
                if (snapshot_) snapshot_(waited);
                lk.lock();
            }
        }
    }

    const char* scope_;
    const char* phase_;
    SnapshotFn snapshot_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool done_ = false;
    std::thread thr_;
};

}
