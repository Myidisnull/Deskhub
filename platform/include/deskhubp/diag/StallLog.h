#pragma once
#include "deskhubp/diag/Log.h"
#include "deskhubp/system/Clock.h"

#include <atomic>
#include <cstdint>
#include <functional>
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

class StopAnrWatch {
public:
    using SnapshotFn = std::function<void(uint32_t waitedMs)>;

    StopAnrWatch(const char* scope, const char* phase, SnapshotFn snapshot = {})
        : scope_(scope), phase_(phase), snapshot_(std::move(snapshot)) {
        thr_ = std::thread([this] { Run(); });
    }
    ~StopAnrWatch() {
        done_.store(true, std::memory_order_release);
        if (thr_.joinable()) thr_.join();
    }
    StopAnrWatch(const StopAnrWatch&) = delete;
    StopAnrWatch& operator=(const StopAnrWatch&) = delete;

private:
    void Run() {
        uint32_t waited = 0;
        bool logged = false;
        while (!done_.load(std::memory_order_acquire)) {
            SleepUs(100'000);
            if (done_.load(std::memory_order_acquire)) return;
            waited += 100;
            if (!logged && waited >= kStopAnrMs) {
                LOGE("[DIAG][%s] evt=stop_anr phase=%s ms=%u state=blocked", scope_, phase_,
                    waited);
                if (snapshot_) snapshot_(waited);
                logged = true;
            } else if (logged && waited % 5000u == 0u) {
                LOGE("[DIAG][%s] evt=stop_anr phase=%s ms=%u state=still_blocked", scope_,
                    phase_, waited);
                if (snapshot_) snapshot_(waited);
            }
        }
    }

    const char* scope_;
    const char* phase_;
    SnapshotFn snapshot_;
    std::atomic<bool> done_{false};
    std::thread thr_;
};

}
