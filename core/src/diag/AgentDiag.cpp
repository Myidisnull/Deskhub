// =============================================================================
// AgentDiag.cpp — dựng các dòng log mỗi giây của host.
//
// Cùng ba nguyên tắc với ClientDiag.cpp (nối bằng con trỏ chạy, Append tự dừng
// khi hết chỗ, thứ tự trường là hợp đồng với người đọc). Riêng ở đây thêm một
// điều: evt=idr là dòng CHỐT-RỒI-IN — thread Encode chốt số, vòng Recv in ra —
// nên nó có một cặp store/exchange chứ không phải bộ đếm cửa sổ như phần còn lại.
//
// LIÊN QUAN: deskhub/diag/AgentDiag.h (thiết kế + lý do gom về core)
// =============================================================================
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
    // Lần chốt ĐẦU TIÊN chỉ ghi mốc: không có cửa sổ trước để lấy hiệu, và chia cho
    // một khoảng thời gian bịa ra sẽ cho một con số fps vô nghĩa ở giây đầu phiên.
    if (lastUs_) {
        // Dùng độ dài THẬT của cửa sổ chứ không phải hằng số 1s: vòng Recv bị
        // recvfrom chặn nên cửa sổ hay dài hơn 1s một chút, chia theo hằng số sẽ
        // thổi phồng fps/kbps. Cùng lập luận với LinkStats::Close ở phía client.
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

// Bản sao của Append trong ClientDiag.cpp — cùng hợp đồng: `p` trỏ vào '\0'
// hiện tại, hết chỗ thì mọi lời gọi sau thành không-làm-gì.
void Append(char*& p, char* end, const char* fmt, ...) {
    if (p >= end) return;
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(p, size_t(end - p), fmt, ap);
    va_end(ap);
    p = (n < 0 || n >= int(end - p)) ? end : p + n;
}

} // namespace

void SourceDiag::LatchIdr(uint64_t bytes, uint32_t pkts, uint32_t burstMs) {
    // bytes ghi SAU CÙNG với release: vòng Recv đọc bytes trước (acquire) và chỉ
    // đọc pkts/burst khi bytes khác 0, nên thứ tự này bảo đảm nó không bắt được
    // một bản ghi nửa vời.
    idrPkts_.store(pkts, std::memory_order_relaxed);
    idrBurstMs_.store(burstMs, std::memory_order_relaxed);
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
    // Đọc-và-xoá hết trước khi in — cùng lý do với ClientDiag::FormatSum.
    const WindowStat::Snapshot e = encMs.TakeReset();
    const WindowStat::Snapshot l = encLatMs.TakeReset();
    const uint32_t idrN = idr.TakeReset();
    const uint32_t fail = sendFail.TakeReset();
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
    if (caps_.zerocopy) Append(p, end, " zerocopy=%d", zerocopy ? 1 : 0);
    return buf;
}

const char* SourceDiag::FormatStatus(char* buf, size_t cap, const char* hms, const char* name,
    const char* state, const Window& w, const LinkView& link) {
    if (!cap) return buf;
    char* p = buf;
    char* const end = buf + cap;
    *p = '\0';

    // %-9s cho state: các tên trạng thái dài ngắn khác nhau, căn trái cố định
    // giữ mọi thứ phía sau thẳng cột qua nhiều dòng liên tiếp.
    Append(p, end, "[Agent t=%s][%s] %-9s | capture %.0f fps | send %.0f fps, %.0f kbps", hms,
        name, state, w.rate.captureFps, w.rate.sendFps, w.rate.sendKbps);
    // `applied` là thống kê MẠNG (event tới nơi và được giao cho injector), KHÔNG
    // phải bằng chứng phím đã tới ứng dụng — injector còn vứt tiếp khi "host
    // thắng", và `skipped` là con số duy nhất lộ ra chuyện đó. Thiếu nó thì "gõ
    // không ăn" không phân biệt được với "không nhận được gói".
    Append(p, end, " | input %" PRIu64 " (lost %" PRIu64 ", skipped %" PRIu64 ")", w.inputApplied,
        w.inputLost, w.inputSkipped);

    // Nửa cuối là SỐ LIỆU CỦA CLIENT, thứ duy nhất host biết về đầu kia.
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

} // namespace deskhub::diag
