#include "deskhub/diag/AgentDiag.h"

#include <cinttypes>
#include <cstdarg>
#include <cstdio>

namespace deskhub::diag {

const char* StateName(HostSession::State s) {
    switch (s) {
        case HostSession::State::Idle: return "IDLE";
        case HostSession::State::Ready: return "READY";
        case HostSession::State::Streaming: return "STREAMING";
    }
    return "?";
}

SourceRate::Window SourceRate::Close(uint32_t captured, uint64_t framesSent, uint64_t bytesSent,
    uint64_t nowUs) {
    Window w;
    if (lastUs_) {
        w.secs = (nowUs - lastUs_) / 1e6;
        if (w.secs > 0.0) {
            w.captureFps = (captured - lastCaptured_) / w.secs;
            w.sendFps = double(framesSent - lastFrames_) / w.secs;
            w.sendKbps = double(bytesSent - lastBytes_) * 8.0 / 1000.0 / w.secs;
        }
    }
    lastCaptured_ = captured;
    lastFrames_ = framesSent;
    lastBytes_ = bytesSent;
    lastUs_ = nowUs;
    return w;
}

namespace {

void Append(char*& p, char* end, const char* fmt, ...) {
    if (p >= end) return;
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(p, size_t(end - p), fmt, ap);
    va_end(ap);
    p = (n < 0 || n >= int(end - p)) ? end : p + n;
}

}

void SourceDiag::LatchIdr(uint64_t bytes, uint32_t pkts, uint32_t burst) {
    idrPkts_.store(pkts, std::memory_order_relaxed);
    idrBurstMs_.store(burst, std::memory_order_relaxed);
    idrBytes_.store(bytes, std::memory_order_release);
}

const char* SourceDiag::FormatIdr(char* buf, size_t cap, const char* name) {
    const uint64_t bytes = idrBytes_.exchange(0, std::memory_order_acquire);
    if (!bytes) return nullptr;
    std::snprintf(buf, cap, "[DIAG][%s] evt=idr bytes=%" PRIu64 " pkts=%u burst_ms=%u", name,
        bytes, idrPkts_.load(std::memory_order_relaxed),
        idrBurstMs_.load(std::memory_order_relaxed));
    return buf;
}

const char* SourceDiag::FormatSum(char* buf, size_t cap, const char* hms, const char* name,
    uint32_t capIdle, bool zerocopy) {
    const WindowStat::Snapshot e = encMs.TakeReset();
    const WindowStat::Snapshot l = encLatMs.TakeReset();
    const uint32_t idrN = idr.TakeReset();
    const uint32_t fail = sendFail.TakeReset();
    const uint32_t queued = queueDrop.TakeReset();
    const uint32_t burst = burstMs.TakeReset();

    if (!cap) return buf;
    char* p = buf;
    char* const end = buf + cap;
    *p = '\0';

    Append(p, end, "[DIAG][%s] evt=sum t=%s enc_ms_avg=%.1f enc_ms_max=%u", name, hms, e.avg,
        e.max);
    Append(p, end, " enc_lat_ms=%.1f/%u", l.avg, l.max);
    if (caps_.capIdle) Append(p, end, " cap_idle=%u", capIdle);
    Append(p, end, " idr=%u burst_ms_max=%u send_fail=%u", idrN, burst, fail);
    if (caps_.queueDrop) Append(p, end, " q_drop=%u", queued);
    if (caps_.zerocopy) Append(p, end, " zerocopy=%d", zerocopy ? 1 : 0);
    return buf;
}

const char* SourceDiag::FormatStatus(char* buf, size_t cap, const char* hms, const char* name,
    const char* state, const Window& w, const LinkView& link) {
    if (!cap) return buf;
    char* p = buf;
    char* const end = buf + cap;
    *p = '\0';

    Append(p, end, "[Agent t=%s][%s] %-9s | capture %.0f fps | send %.0f fps, %.0f kbps", hms,
        name, state, w.rate.captureFps, w.rate.sendFps, w.rate.sendKbps);
    Append(p, end, " | input %" PRIu64 " (lost %" PRIu64 ", skipped %" PRIu64 ")", w.inputApplied,
        w.inputLost, w.inputSkipped);

    if (link.have)
        Append(p, end, " | client loss %u%%, RTT %u ms, recv %u kbps", link.lossPct, link.rttMs,
            link.recvKbps);
    else
        Append(p, end, " | client -");
    return buf;
}

const char* AgentDiag::FormatSum(char* buf, size_t cap, const char* hms) {
    std::snprintf(buf, cap, "[DIAG][agent] evt=sum t=%s loop_busy_ms_max=%u", hms,
        loopBusyMs.TakeReset());
    return buf;
}

}
