#pragma once
// =============================================================================
// AgentDiag.h — TOÀN BỘ số liệu chẩn đoán phía host, một bản duy nhất.
//
// NHIỆM VỤ
//   Đối ứng của ClientDiag.h ở đầu bên kia. Giữ bộ đếm cửa sổ 1s của host và
//   dựng ba dòng log:
//     [Agent t=…][<nguồn>] …          — trạng thái một nguồn, người đọc được
//     [DIAG][<nguồn>] evt=sum t=… …   — số liệu encode/gửi của nguồn đó
//     [DIAG][agent] evt=sum t=… …     — sức khoẻ vòng Recv, chung mọi nguồn
//   cộng thêm [DIAG][<nguồn>] evt=idr — chốt một frame IDR vừa rời host.
//
// ⚠ VÌ SAO NẰM Ở CORE (chuyển vào 31/07/2026)
//   Ba host — Windows, macOS, Ubuntu — chép nguyên khối bộ biến dg*, hàm
//   DiagAtomicMax (định nghĩa BA LẦN: Diag.h của Windows, và inline trong
//   AgentLoop.cpp của macOS lẫn Ubuntu) và các chuỗi định dạng. Lý do đầy đủ ở
//   ClientDiag.h; ở đây thêm một lý do riêng: dòng [Agent] của Ubuntu đã trôi
//   mất phần đuôi `| client loss…, RTT…, recv…` mà hai host kia có, nên host
//   Ubuntu mù đúng cái thông tin "biết gì về đầu kia". Một hàm dựng chuỗi thì
//   không có chỗ nào để trôi.
//
//   Như ClientDiag: phép ĐO ở lại từng host (nó gọi vào VideoToolbox, VA-API,
//   Media Foundation), phép GOM và phép DỰNG CHUỖI về đây.
//
// KHÔNG GHI FILE, KHÔNG printf, KHÔNG CẤP PHÁT — cùng ràng buộc với ClientDiag.h.
//
// TRƯỜNG THEO NỀN
//   cap_idle chỉ macOS (ScreenCaptureKit báo "không có nội dung mới"),
//   zerocopy chỉ Ubuntu (capture có thương lượng được dma-buf không). Khai báo
//   bằng AgentDiagCaps lúc dựng.
//
// LIÊN QUAN: deskhub/diag/WindowStat.h, deskhub/diag/ClientDiag.h (bản đối
//            ứng phía client), docs/09-diagnostics.md
// =============================================================================
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "deskhub/diag/WindowStat.h"
#include "deskhub/session/HostSession.h"

namespace deskhub::diag {

// Tên trạng thái phiên để in lên dòng [Agent]. Ba host trước đây mỗi bên tự viết
// lại đúng cái switch này; nó thuộc về chỗ cạnh chính enum đó.
const char* StateName(HostSession::State s);

// Bộ đếm TÍCH LUỸ của một nguồn, quy đổi thành số liệu MỘT CỬA SỔ.
//
// Đối ứng phía host của deskhub::LinkStats ở phía client, và cùng một tính chất:
// Close() có TÁC DỤNG PHỤ (chốt ảnh chụp mới), gọi hai lần liên tiếp thì lần thứ
// hai ra toàn số 0.
//
// Ba host trước đây mỗi bên tự giữ lastCaptured/lastBytes/lastFrames rồi tự lấy
// hiệu và chia — ba bản của cùng một phép tính, và bản Windows còn tính lại lần
// nữa cho UI ở một thời điểm khác, cho ra hai con số lệch nhau cho cùng một giây.
class SourceRate {
public:
    struct Window {
        double secs = 0.0;
        double captureFps = 0.0;
        double sendFps = 0.0;
        double sendKbps = 0.0;
    };

    // `captured`/`framesSent`/`bytesSent` là bộ đếm TÍCH LUỸ từ đầu phiên.
    // `nowUs` phải là mốc của chính lần chốt này.
    Window Close(uint32_t captured, uint64_t framesSent, uint64_t bytesSent, uint64_t nowUs);

private:
    uint32_t lastCaptured_ = 0;
    uint64_t lastFrames_ = 0;
    uint64_t lastBytes_ = 0;
    uint64_t lastUs_ = 0; // 0 = chưa chốt lần nào
};

// Trường nào có mặt trên nền này. Mặc định không có cái nào — đúng cho Windows.
struct AgentDiagCaps {
    bool capIdle = false;  // macOS: số lần ScreenCaptureKit gọi mà không có frame mới
    bool zerocopy = false; // Ubuntu: capture đi dma-buf (1) hay chép qua RAM (0)
};

// -----------------------------------------------------------------------------
// Số liệu của MỘT nguồn đang chia sẻ (một SourcePipeline).
// -----------------------------------------------------------------------------
class SourceDiag {
public:
    static constexpr size_t kSumBufBytes = 384;
    static constexpr size_t kStatusBufBytes = 384;
    static constexpr size_t kIdrBufBytes = 160;

    explicit SourceDiag(AgentDiagCaps caps = {}) : caps_(caps) {}

    // -----------------------------------------------------------------------
    // Phía ĐO — chạy trên thread Encode/Capture.
    // -----------------------------------------------------------------------
    WindowStat encMs;     // thời gian lời gọi Encode (xem docs/09: chỉ là SUBMIT
                          // trên macOS/Windows vì hai bộ nén đó bất đồng bộ)
    WindowStat encLatMs;  // độ trễ THẬT: mốc chụp → NAL nén xong quay ra
    WindowCount idr;      // số frame IDR gửi trong cửa sổ
    WindowCount sendFail; // số lần sendto trả lỗi
    WindowMax burstMs;    // chùm gửi dài nhất của một frame

    // Chốt một frame IDR vừa rời host. Gọi trên thread Encode; dòng log được in
    // sau đó trên vòng Recv (FormatIdr) để I/O không rơi vào luồng nóng.
    void LatchIdr(uint64_t bytes, uint32_t pkts, uint32_t burstMs);

    // -----------------------------------------------------------------------
    // Phía IN — trên vòng Recv, mỗi cửa sổ một lần.
    // -----------------------------------------------------------------------

    // Dòng evt=idr nếu có IDR mới được chốt kể từ lần gọi trước, ngược lại
    // nullptr. Đọc-và-xoá.
    const char* FormatIdr(char* buf, size_t cap, const char* name);

    // Dòng evt=sum của nguồn này. ĐỌC-VÀ-XOÁ mọi bộ đếm cửa sổ ở trên.
    //   capIdle   giá trị từ tầng capture (macOS); bỏ qua nếu caps.capIdle sai.
    //   zerocopy  từ tầng capture (Ubuntu); bỏ qua nếu caps.zerocopy sai.
    const char* FormatSum(char* buf, size_t cap, const char* hms, const char* name,
        uint32_t capIdle, bool zerocopy);

    // Những gì host biết về đầu kia, lấy từ gói FEEDBACK gần nhất (~1s/lần).
    // have = false in ra "client -", để "chưa nghe được gì" không bị đọc nhầm
    // thành "0% mất gói, RTT 0 ms".
    struct LinkView {
        bool have = false;
        uint32_t lossPct = 0;
        uint32_t rttMs = 0;
        uint32_t recvKbps = 0;
    };

    // Số liệu một cửa sổ của nguồn: phần nhịp lấy từ SourceRate::Close(), phần
    // input lấy từ HostSession::inputStats() + injector.
    struct Window {
        SourceRate::Window rate;
        uint64_t inputApplied = 0; // event tới nơi và được giao cho injector
        uint64_t inputLost = 0;    // lỗ hổng số thứ tự
        uint64_t inputSkipped = 0; // event nhường lại vì người ngồi máy đang gõ
    };

    // Dòng [Agent t=…][<nguồn>]. Thuần định dạng, không đụng bộ đếm nào.
    static const char* FormatStatus(char* buf, size_t cap, const char* hms, const char* name,
        const char* state, const Window& w, const LinkView& link);

private:
    AgentDiagCaps caps_;
    // 0 = chưa có IDR nào chờ in. Một IDR mới đè lên IDR cũ chưa kịp in: trong
    // một giây mà có nhiều IDR thì cái gần nhất là cái đáng xem.
    std::atomic<uint64_t> idrBytes_{0};
    std::atomic<uint32_t> idrPkts_{0};
    std::atomic<uint32_t> idrBurstMs_{0};
};

// -----------------------------------------------------------------------------
// Số liệu CHUNG của vòng Recv, không thuộc nguồn nào.
// -----------------------------------------------------------------------------
class AgentDiag {
public:
    static constexpr size_t kSumBufBytes = 96;

    WindowMax loopBusyMs; // vòng Recv bận nhất trong cửa sổ

    // Dòng [DIAG][agent] evt=sum. Đọc-và-xoá loopBusyMs.
    const char* FormatSum(char* buf, size_t cap, const char* hms);
};

} // namespace deskhub::diag
