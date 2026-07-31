// =============================================================================
// ClientDiag.cpp — dựng hai dòng log mỗi giây của viewer.
//
// Cả file chỉ là snprintf, nhưng có ba điều đáng nhớ:
//
//   1. Nối chuỗi bằng con trỏ chạy + chỗ còn lại, KHÔNG bằng một chuỗi định dạng
//      khổng lồ. Hai trường (present_ms, disp_drop) chỉ có trên vài nền, mà
//      printf không có "trường tuỳ chọn" — cách duy nhất còn lại là ba bốn bản
//      chuỗi định dạng gần giống nhau, đúng thứ vừa được xoá đi khỏi các client.
//
//   2. Append() tự dừng khi hết chỗ. Dòng bị cắt vẫn hơn tràn buffer, và cỡ
//      kSumBufBytes đã tính dư nên chuyện đó không xảy ra trong thực tế.
//
//   3. Thứ tự trường là HỢP ĐỒNG với người đọc log và với mọi script grep:
//      asm → dec → present → hai bộ đếm vứt frame → gói về muộn → sức khoẻ
//      đường/vòng lặp → độ trễ. Đi theo đúng đường dữ liệu, từ lúc gói tới cho
//      tới lúc hình lên màn.
//
// LIÊN QUAN: deskhub/diag/ClientDiag.h (thiết kế + lý do gom về core)
// =============================================================================
#include "deskhub/diag/ClientDiag.h"

#include <cinttypes>
#include <cstdarg>
#include <cstdio>

namespace deskhub::diag {
namespace {

// Nối vào cuối buffer, dời con trỏ. Hết chỗ thì thôi, không bao giờ ghi quá.
// `p` luôn trỏ vào ký tự kết thúc '\0' hiện tại của buffer.
void Append(char*& p, char* end, const char* fmt, ...) {
    if (p >= end) return;
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(p, size_t(end - p), fmt, ap);
    va_end(ap);
    // n < 0 là lỗi định dạng; n >= chỗ còn lại nghĩa là đã bị cắt — cả hai đều
    // đẩy p tới `end` để mọi lời gọi sau thành không-làm-gì.
    p = (n < 0 || n >= int(end - p)) ? end : p + n;
}

} // namespace

const char* ClientDiag::FormatSum(char* buf, size_t cap, const char* hms, const LinkWindow& w,
    uint32_t gapMsMax, int64_t e2eUs) {
    // ĐỌC-VÀ-XOÁ hết trước khi in: làm ngược lại (đọc rải rác giữa các Append)
    // thì một cửa sổ dài bất thường sẽ nhặt thêm mẫu của cửa sổ sau vào giữa dòng.
    const WindowStat::Snapshot a = asmMs.TakeReset();
    const WindowStat::Snapshot d = decMs.TakeReset();
    const WindowStat::Snapshot pr = presentMs.TakeReset();
    const uint32_t dq = dqDrop.TakeReset();
    const uint32_t disp = dispDrop.TakeReset();
    const uint32_t busy = loopBusyMs.TakeReset();

    if (!cap) return buf;
    char* p = buf;
    char* const end = buf + cap;
    *p = '\0';

    Append(p, end, "[DIAG] evt=sum t=%s asm_ms=%.1f/%u dec_ms=%.1f/%u", hms, a.avg, a.max, d.avg,
        d.max);
    if (caps_.presentMs) Append(p, end, " present_ms=%.1f/%u", pr.avg, pr.max);

    // Hai bộ đếm vứt frame đi liền nhau: chúng trả lời cùng một câu hỏi ("frame
    // rơi ở đâu trong máy này") và trước đây nằm cách nhau nửa dòng.
    Append(p, end, " dq_drop=%u", dq);
    if (caps_.dispDrop) Append(p, end, " disp_drop=%u", disp);

    Append(p, end, " late=%" PRIu64 " late_ms_avg=%.0f late_ms_max=%" PRIu64, w.latePackets,
        w.lateMsAvg, w.lateMsMax);
    Append(p, end, " gap_ms_max=%u loop_busy_ms_max=%u", gapMsMax, busy);
    Append(p, end, " min_rtt_ms=%.1f e2e_ms=%.1f", minRttUs.value() / 1000.0,
        e2eUs >= 0 ? e2eUs / 1000.0 : 0.0);
    return buf;
}

const char* ClientDiag::FormatStatus(char* buf, size_t cap, const char* hms, const LinkWindow& w,
    uint32_t rttUs, int64_t e2eUs) {
    // Bề rộng cố định (%2.0f, %6.0f, %4.1f) để các dòng liên tiếp thẳng cột —
    // mắt bắt được chỗ tụt fps mà không phải đọc từng số.
    std::snprintf(buf, cap,
        "[Client t=%s] %2.0f fps | %6.0f kbps | dropped %" PRIu64
        " frame | lost %4.1f%% pkts"
        " | fec+%" PRIu64 " | RTT %.1f ms | e2e ~%.1f ms",
        hms, w.fps, w.kbps, w.framesDropped, w.lossPct, w.packetsRecovered, rttUs / 1000.0,
        e2eUs >= 0 ? e2eUs / 1000.0 : 0.0);
    return buf;
}

const char* ClientDiag::FormatCompact(char* buf, size_t cap, const LinkWindow& w, uint32_t rttUs,
    int64_t e2eUs, const char* sep) {
    // Mbps chứ không phải kbps như dòng log: đây là thứ người dùng liếc qua trong
    // lúc đang xem, không phải thứ đem đi so sánh giữa hai file log.
    std::snprintf(buf, cap, "%.0f fps%s%.1f Mbps%sloss %.1f%%%sRTT %.0f ms%se2e %.0f ms", w.fps,
        sep, w.kbps / 1000.0, sep, w.lossPct, sep, rttUs / 1000.0, sep,
        e2eUs >= 0 ? e2eUs / 1000.0 : 0.0);
    return buf;
}

const char* ClientDiag::FormatFrameDrop(char* buf, size_t cap,
    const Reassembler::FrameDropInfo& d) {
    // Thứ tự khớp Reassembler::DropReason. Bảng ở đây chứ không ở core/transport vì
    // đây là chuyện HIỂN THỊ, còn Reassembler thì không được biết tới chuỗi log.
    static const char* const kReason[] = {"timeout", "overtaken", "evicted", "pre_idr"};
    const size_t r = size_t(d.reason);

    // Chùm thiếu nằm ở đâu trong frame. `tail` là chữ ký của mất gói theo cụm
    // (docs/06 §5): gói cuối của frame bị nuốt cả loạt, khác hẳn mất rải rác.
    // "-" khi không thiếu mảnh nào — ca pre_idr (frame lành bị nuốt vì đang chờ IDR).
    const char* pos = "-";
    if (d.missing) {
        const bool head = d.firstMissing == 0;
        const bool tail = d.lastMissing + 1 == d.total;
        pos = head && tail ? "all" : tail ? "tail"
                                 : head   ? "head"
                                          : "mid";
    }

    std::snprintf(buf, cap,
        "[DIAG] evt=frame_drop id=%u reason=%s miss=%u/%u pos=%s idr=%u waited_ms=%u "
        "got_bytes=%u",
        d.frameId, r < 4 ? kReason[r] : "?", d.missing, d.total, pos, d.idr ? 1 : 0, d.waitedMs,
        d.bytesGot);
    return buf;
}

const char* KeyframeRequestLog::Request(char* buf, size_t cap, uint64_t nowUs,
    const char* reason) {
    if (reqUs_) return nullptr; // đã treo sẵn: lần phát lại, không log lại
    // nowUs == 0 về lý thuyết không xảy ra (đồng hồ đơn điệu từ lúc khởi động),
    // nhưng 0 là giá trị "không treo" nên phải tránh dùng nó làm mốc.
    reqUs_ = nowUs ? nowUs : 1;
    std::snprintf(buf, cap, "[DIAG] evt=kf_req reason=%s", reason);
    return buf;
}

const char* KeyframeRequestLog::Arrived(char* buf, size_t cap, uint64_t nowUs, size_t idrBytes) {
    if (!reqUs_) return nullptr; // IDR định kỳ chứ không phải ta xin
    const uint64_t afterMs = nowUs > reqUs_ ? (nowUs - reqUs_) / 1000 : 0;
    reqUs_ = 0;
    // after_ms là thời gian NGƯỜI DÙNG nhìn một khung hình đứng yên — con số đáng
    // giá nhất của cặp sự kiện này.
    std::snprintf(buf, cap, "[DIAG] evt=idr_rx bytes=%zu after_ms=%" PRIu64, idrBytes, afterMs);
    return buf;
}

} // namespace deskhub::diag
