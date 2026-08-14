#pragma once
#include <cstddef>
#include <cstdint>

#include "deskhub/control/LinkStats.h"
#include "deskhub/diag/WindowStat.h"
#include "deskhub/transport/Reassembler.h"

namespace deskhub::diag {

struct ClientDiagCaps {
    bool presentMs = false;
    bool dispDrop = false;
};

class ClientDiag {
public:
    static constexpr size_t kSumBufBytes = 512;
    static constexpr size_t kStatusBufBytes = 256;

    explicit ClientDiag(ClientDiagCaps caps = {})
        : caps_(caps) {}

    WindowStat asmMs;
    WindowStat decMs;
    WindowStat presentMs;
    WindowCount dqDrop;
    WindowCount dispDrop;
    WindowMax loopBusyMs;
    RunningMin minRttUs;

    const char* FormatSum(char* buf, size_t cap, const char* hms, const LinkWindow& w,
        uint32_t gapMsMax, int64_t e2eUs);

    static const char* FormatStatus(char* buf, size_t cap, const char* hms, const LinkWindow& w,
        uint32_t rttUs, int64_t e2eUs);

    static const char* FormatCompact(char* buf, size_t cap, const LinkWindow& w, uint32_t rttUs,
        int64_t e2eUs, const char* sep = "  ");

    static const char* FormatFrameDrop(char* buf, size_t cap,
        const Reassembler::FrameDropInfo& d);

    static constexpr size_t kFrameDropBufBytes = 192;
    static constexpr size_t kCompactBufBytes = 160;

private:
    ClientDiagCaps caps_;
};

class KeyframeRequestLog {
public:
    static constexpr size_t kBufBytes = 96;

    const char* Request(char* buf, size_t cap, uint64_t nowUs, const char* reason);

    const char* Arrived(char* buf, size_t cap, uint64_t nowUs, size_t idrBytes);

    bool pending() const {
        return reqUs_ != 0;
    }

private:
    uint64_t reqUs_ = 0;
};

}
