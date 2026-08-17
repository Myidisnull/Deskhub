#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "deskhubp/input/LocalInput.h"

#include <windows.h>

#include <atomic>
#include <thread>

#include "deskhubp/diag/Log.h"
#include "deskhubp/diag/StallLog.h"
#include "deskhubp/system/Clock.h"

namespace {

std::atomic<uint64_t> g_lastPhysicalUs{0};

LRESULT CALLBACK KeyboardHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const auto* k = (const KBDLLHOOKSTRUCT*)lp;
        if (!(k->flags & LLKHF_INJECTED))
            g_lastPhysicalUs.store(NowUs(), std::memory_order_relaxed);
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT CALLBACK MouseHook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const auto* m = (const MSLLHOOKSTRUCT*)lp;
        if (!(m->flags & LLMHF_INJECTED))
            g_lastPhysicalUs.store(NowUs(), std::memory_order_relaxed);
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

}

struct LocalInputMonitor::Impl {
    std::thread thread;
    std::atomic<bool> quit{false};
    std::atomic<DWORD> threadId{0};

    void Run() {
        MSG warm{};
        PeekMessageW(&warm, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
        threadId.store(GetCurrentThreadId(), std::memory_order_release);

        const HHOOK kb = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHook,
            GetModuleHandleW(nullptr), 0);
        const HHOOK ms = SetWindowsHookExW(WH_MOUSE_LL, MouseHook,
            GetModuleHandleW(nullptr), 0);

        if ((kb || ms) && !quit.load(std::memory_order_acquire)) {
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        if (kb) UnhookWindowsHookEx(kb);
        if (ms) UnhookWindowsHookEx(ms);
        threadId.store(0, std::memory_order_release);
    }
};

LocalInputMonitor::LocalInputMonitor()
    : impl_(std::make_unique<Impl>()) {}

LocalInputMonitor::~LocalInputMonitor() {
    Stop();
}

void LocalInputMonitor::Start() {
    if (impl_->thread.joinable()) return;
    impl_->quit.store(false, std::memory_order_release);
    impl_->thread = std::thread([im = impl_.get()] { im->Run(); });
}

void LocalInputMonitor::Stop() {
    const uint64_t t0 = NowUs();
    deskhubp::StopAnrWatch watch("agent", "local_input_join");
    impl_->quit.store(true, std::memory_order_release);
    DWORD tid = impl_->threadId.load(std::memory_order_acquire);
    for (uint32_t waited = 0; !tid && impl_->thread.joinable() && waited < 500; waited += 10) {
        SleepUs(10'000);
        tid = impl_->threadId.load(std::memory_order_acquire);
    }
    if (tid) {
        bool posted = false;
        for (int attempt = 0; attempt < 50 && !posted; ++attempt) {
            if (PostThreadMessageW(tid, WM_QUIT, 0, 0)) {
                posted = true;
                break;
            }
            const DWORD err = GetLastError();
            if (attempt == 0 || attempt == 49) {
                LOGW("[DIAG][agent] evt=local_input_quit_fail tid=%lu err=%lu attempt=%d",
                    (unsigned long)tid, (unsigned long)err, attempt);
            }
            SleepUs(10'000);
        }
    } else if (impl_->thread.joinable()) {
        LOGW("[DIAG][agent] evt=local_input_quit_no_tid");
    }
    if (impl_->thread.joinable()) impl_->thread.join();
    impl_->threadId.store(0, std::memory_order_release);
    deskhubp::LogStopPhase("agent", "local_input_join", t0);
}

uint64_t LocalInputMonitor::lastLocalUs() const {
    return g_lastPhysicalUs.load(std::memory_order_relaxed);
}
