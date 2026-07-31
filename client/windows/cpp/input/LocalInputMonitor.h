#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <thread>

class LocalInputMonitor {
public:
    LocalInputMonitor() = default;
    ~LocalInputMonitor() {
        Stop();
    }
    LocalInputMonitor(const LocalInputMonitor&) = delete;
    LocalInputMonitor& operator=(const LocalInputMonitor&) = delete;

    void Start();

    void Stop();

    static uint64_t LastPhysicalUs();

private:
    void ThreadMain();

    std::thread thread_;
    std::atomic<bool> quit_{false};
    std::atomic<DWORD> threadId_{0};
};
