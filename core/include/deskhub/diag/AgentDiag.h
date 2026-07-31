#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "deskhub/diag/WindowStat.h"
#include "deskhub/session/HostSession.h"

namespace deskhub::diag {

const char* StateName(HostSession::State s);

class SourceRate {
public:
    struct Window {
        double secs = 0.0;
        double captureFps = 0.0;
        double sendFps = 0.0;
        double sendKbps = 0.0;
    };

    Window Close(uint32_t captured, uint64_t framesSent, uint64_t bytesSent, uint64_t nowUs);

private:
    uint32_t lastCaptured_ = 0;
    uint64_t lastFrames_ = 0;
    uint64_t lastBytes_ = 0;
    uint64_t lastUs_ = 0;
};

struct AgentDiagCaps {
    bool capIdle = false;
    bool zerocopy = false;
};

class SourceDiag {
public:
    static constexpr size_t kSumBufBytes = 384;
    static constexpr size_t kStatusBufBytes = 384;
    static constexpr size_t kIdrBufBytes = 160;

    explicit SourceDiag(AgentDiagCaps caps = {}) : caps_(caps) {}

    WindowStat encMs;
    WindowStat encLatMs;
    WindowCount idr;
    WindowCount sendFail;
    WindowMax burstMs;

    void LatchIdr(uint64_t bytes, uint32_t pkts, uint32_t burstMs);

    const char* FormatIdr(char* buf, size_t cap, const char* name);

    const char* FormatSum(char* buf, size_t cap, const char* hms, const char* name,
        uint32_t capIdle, bool zerocopy);

    struct LinkView {
        bool have = false;
        uint32_t lossPct = 0;
        uint32_t rttMs = 0;
        uint32_t recvKbps = 0;
    };

    struct Window {
        SourceRate::Window rate;
        uint64_t inputApplied = 0;
        uint64_t inputLost = 0;
        uint64_t inputSkipped = 0;
    };

    static const char* FormatStatus(char* buf, size_t cap, const char* hms, const char* name,
        const char* state, const Window& w, const LinkView& link);

private:
    AgentDiagCaps caps_;
    std::atomic<uint64_t> idrBytes_{0};
    std::atomic<uint32_t> idrPkts_{0};
    std::atomic<uint32_t> idrBurstMs_{0};
};

class AgentDiag {
public:
    static constexpr size_t kSumBufBytes = 96;

    WindowMax loopBusyMs;

    const char* FormatSum(char* buf, size_t cap, const char* hms);
};

}
