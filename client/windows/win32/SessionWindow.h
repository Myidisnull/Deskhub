#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "deskhubp/session/AgentDriver.h"
#include "deskhubp/session/AgentLoop.h"
#include "SessionRow.h"

class SessionWindow {
public:
    SessionWindow() = default;
    ~SessionWindow() {
        Stop();
    }
    SessionWindow(const SessionWindow&) = delete;
    SessionWindow& operator=(const SessionWindow&) = delete;

    void Start();

    void AttachAgent(AgentLoop& agent);

    void Stop();

    bool stopRequested() const {
        return stopReq_.load(std::memory_order_acquire);
    }

private:
    void ThreadMain();
    LRESULT HandleMsg(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK WndProcThunk(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    void PullRows();
    void RefreshList();

    std::atomic<AgentLoop*> agent_{nullptr};
    deskhubp::AgentDriver driver_;
    std::thread thread_;
    std::atomic<bool> stopReq_{false};
    std::atomic<bool> quitReq_{false};
    std::atomic<HWND> hwnd_{nullptr};

    std::vector<SessionSourceRow> uiRows_;
    bool agentAttached_ = false;
    HWND list_ = nullptr;
};

void RunSharingSession(HWND owner, const std::vector<AgentSource>& sources,
    const AgentOptions& opt);
