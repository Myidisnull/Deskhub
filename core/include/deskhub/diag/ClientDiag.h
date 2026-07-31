#pragma once
// =============================================================================
// ClientDiag.h — TOÀN BỘ số liệu chẩn đoán phía client, một bản duy nhất.
//
// NHIỆM VỤ
//   Giữ bộ đếm cửa sổ 1s của viewer (t_asm, t_dec, present, hàng đợi, sức khoẻ
//   vòng lặp, sàn RTT) và DỰNG hai dòng log mỗi giây từ chúng:
//     [Client t=…] …            — dòng trạng thái người đọc được
//     [DIAG] evt=sum t=… …      — dòng số liệu để grep
//
// ⚠ VÌ SAO NẰM Ở CORE (chuyển vào 31/07/2026)
//   Năm viewer — Windows, macOS, Ubuntu, iOS, Android — trước đây mỗi bên tự giữ
//   một bộ biến dg*, tự viết vòng cập nhật max, tự ghép chuỗi định dạng. Năm bản
//   chép tay của cùng một dòng log, và chúng ĐÃ lệch nhau: thứ tự trường khác
//   nhau giữa Windows và nhóm Apple, phép cập nhật max trên Android có đua
//   (xem WindowStat.h). Thêm một trường vào dòng evt=sum nghĩa là sửa đúng năm
//   chỗ và nhớ giữ chúng khớp — việc không ai làm đúng mãi được.
//
//   Cái KHÔNG chuyển được là phép ĐO (gọi đồng hồ, hỏi decoder, hỏi layer hiển
//   thị) — nó dính thiết bị nên ở lại từng client. Cái chuyển được là phép GOM
//   và phép DỰNG CHUỖI, và đó là toàn bộ nội dung file này.
//
// KHÔNG GHI FILE, KHÔNG printf: FormatXxx chỉ dựng chuỗi vào buffer của người
// gọi. core giữ nguyên tắc "core stays I/O-free" của docs/09 — Windows in bằng
// printf (stdout đã bị freopen vào file log), các nền còn lại đi qua LOGI.
// Dựng TRỌN dòng rồi ghi bằng MỘT lời gọi cũng là thứ giữ log của các thread
// không cài răng lược vào nhau.
//
// KHÔNG CẤP PHÁT: buffer do người gọi đưa (char trên stack), snprintf tự cắt
// nếu thiếu chỗ. kSumBufBytes / kStatusBufBytes dưới đây là cỡ đủ dùng.
//
// TRƯỜNG THEO NỀN
//   present_ms chỉ Windows (Present chạy đồng bộ trong Decode), disp_drop chỉ
//   Apple + Android (tầng hiển thị bất đồng bộ mới có khái niệm này). Khai báo
//   bằng ClientDiagCaps lúc dựng; nền không có thì trường biến mất khỏi dòng
//   log thay vì in ra một số 0 vô nghĩa.
//
// LIÊN QUAN: deskhub/diag/WindowStat.h (bốn kiểu bộ đếm),
//            deskhub/control/LinkStats.h (số liệu đường truyền, nguồn của late/
//            loss/fps/kbps), deskhub/control/ClockOffset.h (e2e),
//            docs/09-diagnostics.md
// =============================================================================
#include <cstddef>
#include <cstdint>

#include "deskhub/control/LinkStats.h"
#include "deskhub/diag/WindowStat.h"
#include "deskhub/transport/Reassembler.h"

namespace deskhub::diag {

// Trường nào có mặt trên nền này. Mặc định: không có cái nào — đúng cho Ubuntu.
struct ClientDiagCaps {
    bool presentMs = false; // Windows: thời gian kẹt trong IDXGISwapChain::Present
    bool dispDrop = false;  // Apple + Android: frame rơi ở tầng hiển thị
};

class ClientDiag {
public:
    // Cỡ buffer đủ cho dòng dài nhất (Windows, đủ mọi trường) kể cả khi mọi số
    // đều bung hết chữ số. Dư rộng tay: đây là stack của vòng lặp 1s, không phải
    // luồng nóng.
    static constexpr size_t kSumBufBytes = 512;
    static constexpr size_t kStatusBufBytes = 256;

    explicit ClientDiag(ClientDiagCaps caps = {}) : caps_(caps) {}

    // -----------------------------------------------------------------------
    // Phía ĐO — thread nào cũng gọi được, xem mô hình luồng ở WindowStat.h.
    // -----------------------------------------------------------------------
    WindowStat asmMs;     // t_asm: mảnh đầu tới → frame ghép xong (thread Net)
    WindowStat decMs;     // t_dec: trọn lời gọi Decode (thread Decode)
    WindowStat presentMs; // chỉ Windows, đo quanh Present (thread Decode)
    WindowCount dqDrop;   // frame vứt vì hàng đợi giải mã đầy (thread Net)
    WindowCount dispDrop; // frame vứt vì tầng hiển thị nghẽn (thread Decode)
    WindowMax loopBusyMs; // vòng Net bận nhất trong cửa sổ
    RunningMin minRttUs;  // sàn mạng: RTT nhỏ nhất TỪNG thấy, không reset

    // -----------------------------------------------------------------------
    // Phía IN — gọi ĐÚNG MỘT LẦN mỗi cửa sổ, trên thread Net.
    // -----------------------------------------------------------------------

    // Dòng [DIAG] evt=sum. ĐỌC-VÀ-XOÁ mọi bộ đếm cửa sổ ở trên, nên gọi hai lần
    // liên tiếp sẽ cho lần thứ hai toàn số 0 — cùng tính chất tác dụng phụ với
    // LinkStats::Close().
    //   hms      giờ địa phương "HH:MM:SS" (deskhubp::LocalTimeHms) — core không
    //            tự đọc đồng hồ tường để khỏi kéo header hệ điều hành vào đây.
    //   w        cửa sổ vừa đóng của LinkStats (nguồn của late/late_ms_*).
    //   gapMsMax Reassembler::TakeMaxGapMs() — cũng là hàm đọc-và-xoá.
    //   e2eUs    ước lượng trễ e2e; < 0 nghĩa là chưa có mẫu.
    // Trả về `buf` để dùng thẳng làm tham số của printf/LOGI.
    const char* FormatSum(char* buf, size_t cap, const char* hms, const LinkWindow& w,
        uint32_t gapMsMax, int64_t e2eUs);

    // Dòng [Client t=…] cho người đọc. Thuần định dạng, không đụng bộ đếm nào,
    // nên là hàm static — thứ tự gọi so với FormatSum không quan trọng.
    //   rttUs  RTT MỚI NHẤT (ClientSession::lastRttUs), khác min_rtt_ms ở dòng
    //          evt=sum vốn là kỷ lục cả phiên.
    static const char* FormatStatus(char* buf, size_t cap, const char* hms, const LinkWindow& w,
        uint32_t rttUs, int64_t e2eUs);

    // Bản GỌN cho overlay trên màn hình (log giữ bản đầy đủ ở FormatStatus).
    //   sep  dấu ngăn giữa các mục. Windows dùng "\xC2\xB7" (dấu chấm giữa,
    //        UTF-8) cho khớp phong cách thanh tiêu đề của nó; bốn nền kia dùng
    //        hai dấu cách. Đây là khác biệt THẨM MỸ có thật và là lý do tham số
    //        này tồn tại thay vì ép cả năm nền trông giống nhau.
    static const char* FormatCompact(char* buf, size_t cap, const LinkWindow& w, uint32_t rttUs,
        int64_t e2eUs, const char* sep = "  ");

    // -----------------------------------------------------------------------
    // Sự kiện RỜI RẠC — không theo nhịp 1s, nên chúng là hàm static thuần.
    // -----------------------------------------------------------------------

    // Dòng evt=frame_drop: bản khám nghiệm MỘT frame mà Reassembler đã bỏ.
    //
    // `pos` (head/tail/mid/all) là thứ đáng giá nhất trên dòng này: chùm thiếu nằm
    // ở ĐUÔI là chữ ký của mất gói theo cụm (docs/06 §5), khác hẳn mất rải rác.
    static const char* FormatFrameDrop(char* buf, size_t cap,
        const Reassembler::FrameDropInfo& d);

    static constexpr size_t kFrameDropBufBytes = 192;
    static constexpr size_t kCompactBufBytes = 160;

private:
    ClientDiagCaps caps_;
};

// -----------------------------------------------------------------------------
// Máy trạng thái "đang xin keyframe".
// -----------------------------------------------------------------------------
//
// NHIỆM VỤ
//   Ghép evt=kf_req với evt=idr_rx đi sau nó, để trả lời đúng một câu hỏi: NGƯỜI
//   DÙNG ĐÃ NHÌN MỘT KHUNG HÌNH ĐỨNG YÊN TRONG BAO LÂU. Đó là `after_ms`.
//
// VÌ SAO PHẢI CÓ TRẠNG THÁI
//   Yêu cầu keyframe được PHÁT LẠI liên tục cho tới khi IDR về (gói xin có thể
//   mất). In log mỗi lần phát lại thì một sự cố thành hàng chục dòng và con số
//   after_ms mất nghĩa. Lớp này chỉ ghi nhận lần CHUYỂN từ "không treo" sang
//   "đang treo" — gọi Request() bao nhiêu lần cũng vô hại.
//
// Năm viewer trước đây mỗi bên tự giữ một biến kfReqUs và tự viết lại đúng logic
// này (deskhub/diag/ClientDiag.h, mục vì sao gom về core).
class KeyframeRequestLog {
public:
    static constexpr size_t kBufBytes = 96;

    // Bắt đầu xin keyframe vì `reason`. Trả về dòng evt=kf_req để ghi, hoặc
    // nullptr nếu đang treo sẵn rồi (lần phát lại — không log).
    // `reason` ∈ loss | wait_idr | dec_fail | q_overflow | display_congested
    const char* Request(char* buf, size_t cap, uint64_t nowUs, const char* reason);

    // IDR đã về. Trả về dòng evt=idr_rx kèm after_ms, hoặc nullptr nếu không có
    // yêu cầu nào đang treo (IDR định kỳ chứ không phải ta xin).
    const char* Arrived(char* buf, size_t cap, uint64_t nowUs, size_t idrBytes);

    bool pending() const {
        return reqUs_ != 0;
    }

private:
    uint64_t reqUs_ = 0; // 0 = không có yêu cầu nào đang treo
};

} // namespace deskhub::diag
