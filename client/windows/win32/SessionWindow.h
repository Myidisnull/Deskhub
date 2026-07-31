#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "AgentControl.h"
#include "AgentLoop.h"
#include "SessionRow.h"

class SessionWindow : public AgentControl {
public:
    SessionWindow() = default;
    ~SessionWindow() {
        Stop();
    }
    SessionWindow(const SessionWindow&) = delete;
    SessionWindow& operator=(const SessionWindow&) = delete;

    void Start();

    void Stop();

    bool active() const override {
        return active_.load(std::memory_order_acquire);
    }
    bool stopRequested() const override {
        return stopReq_.load(std::memory_order_acquire);
    }
    void SetRows(std::vector<SessionSourceRow> rows) override;

private:
    void ThreadMain();
    LRESULT HandleMsg(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK WndProcThunk(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    void RefreshList();

    std::thread thread_;
    std::atomic<bool> active_{false};
    std::atomic<bool> stopReq_{false};
    std::atomic<bool> quitReq_{false};
    std::atomic<HWND> hwnd_{nullptr};

    std::mutex m_;
    std::vector<SessionSourceRow> rows_;
    bool dirty_ = false;

    std::vector<SessionSourceRow> uiRows_;
    HWND list_ = nullptr;
};
