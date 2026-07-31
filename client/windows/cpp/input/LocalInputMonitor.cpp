#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "input/LocalInputMonitor.h"

#include "deskhubp/Clock.h"

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

uint64_t LocalInputMonitor::LastPhysicalUs() {
    return g_lastPhysicalUs.load(std::memory_order_relaxed);
}

void LocalInputMonitor::Start() {
    quit_.store(false, std::memory_order_release);
    thread_ = std::thread(&LocalInputMonitor::ThreadMain, this);
}

void LocalInputMonitor::Stop() {
    quit_.store(true, std::memory_order_release);
    if (const DWORD tid = threadId_.load(std::memory_order_acquire))
        PostThreadMessageW(tid, WM_QUIT, 0, 0);
    if (thread_.joinable()) thread_.join();
    threadId_.store(0, std::memory_order_release);
}

void LocalInputMonitor::ThreadMain() {
    const HHOOK kb = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHook,
        GetModuleHandleW(nullptr), 0);
    const HHOOK ms = SetWindowsHookExW(WH_MOUSE_LL, MouseHook,
        GetModuleHandleW(nullptr), 0);
    threadId_.store(GetCurrentThreadId(), std::memory_order_release);

    if ((kb || ms) && !quit_.load(std::memory_order_acquire)) {
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (kb) UnhookWindowsHookEx(kb);
    if (ms) UnhookWindowsHookEx(ms);
}
