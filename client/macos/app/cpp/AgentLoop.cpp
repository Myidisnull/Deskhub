// =============================================================================
// AgentLoop.cpp — vai trò HOST. Nơi mọi thứ của phía chia sẻ được ghép lại.
//                 Port của client/windows/AgentLoop.cpp; ba backend phần cứng đổi
//                 (ScreenCaptureKit / VideoToolbox / CGEvent), phần điều phối giữ
//                 nguyên từng bước.
//
// NHIỆM VỤ
//   Ghép chuỗi bắt hình/mã hoá với tầng mạng, rồi nhân lên cho nhiều nguồn. Đây là
//   file điều phối lớn nhất phía host — bản thân nó không cài đặt thuật toán nào,
//   mà nối các mảnh đã có và quản lý luồng giữa chúng.
//
// ⚠ KIẾN TRÚC LUỒNG — điều quan trọng nhất phải nắm trước khi sửa
//
//   MỖI NGUỒN có một queue frame riêng (do SCStream tạo):
//       capture → encoder.Encode()  ─┐
//                                    ├─ (bất đồng bộ) → onPacket → Packetizer → sock
//   MỘT thread nội bộ của VideoToolbox trả kết quả nén, ĐÃ ĐƯỢC NỐI TIẾP HOÁ bởi
//   VtEncoder::emitMutex_ — nên Packetizer (single-thread, không tự khoá) vẫn an toàn.
//
//   MỘT thread Recv DÙNG CHUNG cho mọi nguồn (do AgentLoop::Start tạo):
//       recvfrom (timeout 100ms) → định tuyến gói → Tick mọi phiên → thống kê 1s/lần
//
//   Nghĩa là với N nguồn thì có N queue capture + 1 thread nén + 1 thread Recv, và
//   mọi trạng thái đi qua ranh giới giữa chúng phải là atomic hoặc được mutex bảo
//   vệ. SourcePipeline bên dưới ghi rõ từng trường thuộc về thread nào — ĐỌC PHẦN
//   ĐÓ trước khi thêm trường mới.
//
// ĐỊNH TUYẾN GÓI ĐẾN — ba loại, ba cách tìm chủ
//   LIST_SOURCES → không thuộc phiên nào; trả SOURCE_LIST liệt kê mọi nguồn.
//   HELLO        → tìm theo hello.sourceId (lúc này chưa có sessionId).
//   Còn lại      → tìm theo sessionId, khớp với phiên của từng nguồn.
//
// VÌ SAO MỘT SOCKET CHUNG, KHÔNG PHẢI MỖI NGUỒN MỘT CỔNG
//   Người dùng chỉ phải mở một cổng và chỉ phải nhớ một địa chỉ; client hỏi
//   LIST_SOURCES là thấy hết. Cái giá là phải tự định tuyến gói như trên.
//
// HAI CƠ CHẾ ĐÁNG CHÚ Ý (giữ nguyên từ bản Windows, vì lý do y hệt)
//   1. forceIdr là ATOMIC FLAG. Đặt từ thread Recv (onStart / onKeyframeRequest),
//      tiêu thụ ở lần Encode kế tiếp trên queue capture. Không gọi thẳng encoder từ
//      thread Recv được — nó thuộc luồng kia (docs/06 §4).
//   2. CACHE FRAME CUỐI. SCStream chỉ phát frame khi nội dung ĐỔI (SCFrameStatus
//      Complete vs Idle). Nguồn đang tĩnh (menu, màn hình đứng im) mà client xin IDR
//      thì không có frame nào để nén — không cache thì client vào xem màn hình tĩnh
//      sẽ đen VĨNH VIỄN. Ở đây cache là một CVPixelBufferRef được RETAIN, không phải
//      bản sao — rẻ hơn nhiều so với chép 12 MB mỗi frame ở 4K, và pool của SCStream
//      đã được đặt queueDepth = 5 để chịu được việc ta giữ một buffer (ScreenCapture.mm).
//
// LIÊN QUAN: AgentLoop.h (AgentSource/AgentOptions/AgentSourceStatus),
//            ClientLoop.cpp (phía đối diện), deskhub/session/HostSession.h,
//            deskhub/transport/Packetizer.h, client/windows/AgentLoop.cpp,
//            docs/06-transport.md §4
// =============================================================================
#include "AgentLoop.h"

#include <CoreVideo/CVPixelBuffer.h> // CVPixelBufferRetain/Release — API C thuần

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <functional>
#include <utility>

#include "Log.h"
#include "input/InputInjector.h"
#include "input/LocalInputMonitor.h"
#include "Permissions.h"
#include "capture/ScreenCapture.h"
#include "encode/VtEncoder.h"
#include "deskhubp/Clock.h"
#include "deskhubp/LogFile.h" // LocalTimeHms — đóng dấu giờ dòng mỗi giây
#include "deskhubp/Random.h"
#include "net/NetInfo.h"
#include "net/UdpSocket.h"

#include "deskhub/control/BitrateController.h"
#include "deskhub/control/QualityLadder.h"
#include "deskhub/session/Beacon.h" // trả lời LIST_SOURCES / PING dò trước phiên
#include "deskhub/session/HostSession.h"
#include "deskhub/transport/Packetizer.h"
#include "deskhub/transport/RetransmitCache.h"

namespace {

// Cập nhật "giá trị lớn nhất từng thấy" trên một atomic. Vòng compare_exchange là
// cách chuẩn: đọc-so-ghi phải nguyên tử, không thì hai thread cùng ghi sẽ nuốt mất
// một mẫu. (Đối ứng DiagAtomicMax trong client/windows/Diag.h.)
inline void DiagAtomicMax(std::atomic<uint32_t>& slot, uint32_t v) {
    uint32_t cur = slot.load(std::memory_order_relaxed);
    while (v > cur && !slot.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
}

const char* StateName(deskhub::HostSession::State s) {
    switch (s) {
        case deskhub::HostSession::State::Idle: return "IDLE";
        case deskhub::HostSession::State::Ready: return "READY";
        case deskhub::HostSession::State::Streaming: return "STREAMING";
    }
    return "?";
}

// Cỡ khung nhỏ nhất còn encode được. Bộ mã hoá phần cứng từ chối khung quá nhỏ, và
// ca gặp thật là người dùng THU NHỎ cửa sổ đang share. Ngưỡng đặt cao hơn mức tối
// thiểu thật một quãng an toàn — cửa sổ nhỏ hơn cỡ này thì có stream được cũng
// chẳng ai xem nổi.
inline constexpr uint32_t kMinEncodeW = 160;
inline constexpr uint32_t kMinEncodeH = 64;

// Toàn bộ trạng thái của MỘT nguồn. Chứa mutex/atomic nên không copy/move được —
// giữ trong vector<unique_ptr>.
struct SourcePipeline {
    SourcePipeline(uint32_t startBps, uint32_t minBps)
        : curBitrateBps(startBps), rate(startBps, minBps) {}

    ~SourcePipeline() {
        if (cachedPb) CVPixelBufferRelease(static_cast<CVPixelBufferRef>(cachedPb));
    }

    // --- Cấu hình, cố định sau khi dựng ---
    uint8_t sourceId = 0;
    uint32_t displayId = 0; // CGDirectDisplayID
    std::string name;

    ScreenCapture capture;
    InputInjector injector;                        // chỉ thread Recv chạm
    std::unique_ptr<deskhub::HostSession> session; // tạo sau khi biết kích thước nguồn
    deskhub::StreamParams offer;                   // chỉ thread Recv chạm
    deskhub::Packetizer packetizer;                // chỉ thread nén (VtEncoder) chạm

    // GĐ7 NACK: kho các datagram video vừa phát, để gửi lại khi client xin. Store từ
    // thread nén (đường phát), Find từ thread Recv (xử lý NACK) → khoá chung.
    deskhub::RetransmitCache retxCache;
    std::mutex retxMutex;

    // --- Chia sẻ giữa queue capture / thread nén và thread Recv ---
    std::atomic<uint32_t> srcW{0}, srcH{0};
    std::atomic<bool> sizeChanged{false};
    // Thang vừa đổi bậc. Tách khỏi sizeChanged vì bậc chỉ-đổi-fps KHÔNG đổi cỡ
    // buffer, nên onFrame không bao giờ thấy — mà client vẫn bắt buộc phải nhận fps
    // mới (deskhub::Reconfig::fps). Thread Recv đặt, chính nó tiêu thụ.
    std::atomic<bool> qualityChanged{false};
    std::atomic<bool> wantFec{false};
    std::atomic<uint32_t> curBitrateBps{0};
    // fps của bậc đang chạy. Thread Recv ghi (thang đổi bậc), queue capture đọc khi
    // dựng lại encoder — không có nó thì encoder dựng lại sau một lần đổi độ phân
    // giải sẽ quay về fps ban đầu và lệch hẳn với cỡ đang phát.
    std::atomic<uint32_t> curFps{0};
    std::atomic<bool> netReady{false};
    // failed = HỎNG THẬT, một chiều: capture không start được, nguồn biến mất.
    // Nguồn coi như chết tới hết phiên.
    std::atomic<bool> failed{false};
    // Đã tắt hẳn (người dùng bấm Stop, hoặc dọn cuối phiên). Chỉ thread Recv chạm —
    // để ShutdownPipeline idempotent, gọi lại lần hai là no-op.
    bool shutdownDone = false;
    // paused = TẠM không encode được (nguồn nhỏ hơn kMinEncode*), HAI CHIỀU. Tách
    // khỏi `failed` vì gộp chung thì cửa sổ thu nhỏ sẽ giết phiên vĩnh viễn: onFrame
    // thoát ngay ở đầu hàm nên không bao giờ thấy cửa sổ mở lại (bài học 21/07/2026
    // của bản Windows).
    std::atomic<bool> paused{false};
    std::atomic<bool> forceIdr{false};
    std::atomic<uint64_t> peerPacked{0}; // NetAddr::Pack của client hiện tại (0 = chưa có)
    std::atomic<uint64_t> bytesSent{0}, framesSent{0};
    std::atomic<uint32_t> captured{0};
    std::atomic<uint32_t> nextFrameId{0};
    // RTT chỉ đo được ở PHÍA CLIENT (nó phát PING và trừ khi PONG về), nên FEEDBACK
    // là đường duy nhất con số đó tới được host — UI cần nó để hiện "máy đang xem
    // cách bao xa". Thread Recv ghi; đọc từ PublishStatus cùng thread, nhưng để
    // atomic cho thống nhất với phần còn lại (giống uiRttMs bản Windows).
    std::atomic<uint32_t> uiRttMs{0};
    // Hai số còn lại của FEEDBACK. CHỈ để in ra log mỗi giây, không lên UI.
    //
    // Trước 30/07/2026 host không ghi lại chất lượng đường truyền ở đâu cả: dòng
    // "Bitrate X -> Y (loss N%, RTT N ms)" chỉ in khi bitrate ĐỔI, nên một đường
    // truyền xấu ỔN ĐỊNH không để lại dấu vết nào trong log host. Khi đó chẩn đoán
    // buộc phải có log của cả hai máy mới nói được gì — mà người dùng thường chỉ
    // gửi một bên.
    std::atomic<uint32_t> uiLossPct{0}, uiRecvKbps{0};
    std::atomic<bool> haveFeedback{false};

    std::mutex encMutex; // bảo vệ encoder + cachedPb giữa hai luồng
    std::unique_ptr<VtEncoder> encoder;
    // Dựng encoder theo kích thước hiện tại. Thread Recv cũng cần gọi (encode lại
    // frame tĩnh khi có yêu cầu IDR) nên phải giữ được sau khi vòng khởi tạo kết
    // thúc. GỌI DƯỚI encMutex.
    std::function<bool(uint32_t, uint32_t)> ensureEncoderFn;

    // Frame cuối, RETAIN chứ không chép — xem "cache frame cuối" ở đầu file.
    void* cachedPb = nullptr; // CVPixelBufferRef
    std::atomic<bool> haveCached{false};
    std::atomic<uint64_t> lastFrameUs{0};
    uint64_t lastKeepaliveUs = 0; // chỉ thread Recv chạm

    // Thang chất lượng (fps + độ phân giải theo băng thông). Dựng SAU khi biết trần
    // — tức sau HELLO, khi cỡ đã chốt — nên phải là con trỏ. Chỉ thread Recv chạm.
    std::unique_ptr<deskhub::QualityLadder> ladder;
    // Bậc đang áp, để log và để biết lần đổi tới có phải đổi độ phân giải không.
    deskhub::QualityStep step;

    // --- Congestion control, chỉ thread Recv chạm ---
    // Policy thuần ở core; curBitrateBps/wantFec ở trên là bản sao atomic cho luồng
    // nén đọc (nó không được chạm vào rate).
    deskhub::BitrateController rate;

    // --- Thống kê cửa sổ 1s, chỉ thread Recv chạm ---
    uint32_t lastCaptured = 0;
    uint64_t lastBytes = 0, lastFrames = 0;
    double statCaptureFps = 0, statSendFps = 0, statSendKbps = 0;

    // --- Chẩn đoán (docs/09): bộ đếm cửa sổ 1s ---
    std::atomic<uint32_t> dgEncMsSum{0}, dgEncMsMax{0}, dgEncCount{0};
    std::atomic<uint32_t> dgBurstMsMax{0}; // thời gian bắn hết gói của MỘT frame
    std::atomic<uint32_t> dgSendFail{0};   // sendto trả lỗi
    std::atomic<uint32_t> dgIdrCount{0};
    // Sự kiện IDR gần nhất — luồng nén ghi, thread Recv in (giữ I/O ngoài đường
    // nóng, bài học Pacer ở docs/06 §7b). bytes==0 = không có sự kiện; ghi bytes
    // CUỐI CÙNG với release để hai trường kia nhìn thấy trước nó.
    // ⚠ ĐỘ TRỄ THẬT CỦA BỘ NÉN — khoảng mù lớn nhất của toàn bộ chuỗi đo (thêm
    //   30/07/2026). `enc_ms` ở trên KHÔNG phải cái này: nó chỉ đo thời gian NỘP
    //   frame, mà cả ba bộ nén đều nén BẤT ĐỒNG BỘ. Frame nằm trong đường ống của
    //   encoder bao lâu trước khi ra thành NAL thì trước nay không ai đếm.
    //   Đây là con số quyết định: một đường ống sâu 4 frame ở 60fps là 67 ms độ trễ
    //   HẰNG SỐ — và hằng số thì e2e phía client (bộ lọc min, xem ClockOffset.h)
    //   trừ mất sạch. Máy đo báo 7 ms trong khi người dùng thấy lag chính là ca đó.
    //   Đo từ mốc CHỤP (tsUs, đi xuyên qua encoder) tới lúc NAL về tay ta.
    std::atomic<uint32_t> dgEncLatSum{0}, dgEncLatMax{0}, dgEncLatCount{0};
    std::atomic<uint64_t> dgIdrBytes{0};
    std::atomic<uint32_t> dgIdrPkts{0}, dgIdrBurstMs{0};

    // Đo thời gian một lần Encode + cộng vào bộ đếm cửa sổ. Gọi từ CẢ HAI luồng.
    // Lưu ý: khác bản Windows, ms ở đây chỉ là thời gian NỘP frame cho VideoToolbox
    // (nó nén bất đồng bộ), nên số này bình thường gần 0 — nó lộ ra khi encoder bị
    // nghẽn và VTCompressionSessionEncodeFrame bắt đầu chặn.
    void DiagEncode(VtEncoder* enc, void* pb, uint64_t tsUs, bool idr) {
        const uint64_t t0 = NowUs();
        const bool ok = enc->Encode(pb, tsUs, idr);
        const uint32_t ms = uint32_t((NowUs() - t0) / 1000);
        dgEncMsSum.fetch_add(ms, std::memory_order_relaxed);
        dgEncCount.fetch_add(1, std::memory_order_relaxed);
        DiagAtomicMax(dgEncMsMax, ms);
        // Encode hỏng trên đường keepalive/IDR tĩnh trước giờ bị nuốt im lặng —
        // nguồn tĩnh mà encoder chết là client trắng hình không dấu vết.
        if (!ok)
            LOGW("[DIAG][%s] evt=enc_fail idr=%d ms=%u", name.c_str(), idr ? 1 : 0, ms);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Trạng thái toàn phiên
// ---------------------------------------------------------------------------
struct AgentLoop::Impl {
    AgentOptions opt;
    UdpSocket sock;
    std::thread recvThread;
    std::atomic<bool> quit{false};

    std::vector<std::unique_ptr<SourcePipeline>> pipes;
    std::vector<SourcePipeline*> live;

    // Cấp sourceId tăng dần và KHÔNG tái dùng id của nguồn đã tắt: client còn cầm
    // SOURCE_LIST cũ mà HELLO lại trúng một nguồn mới toanh thì xem nhầm màn hình.
    uint8_t nextSourceId = 0;

    // Không có hộp thư lệnh từ UI (bỏ 2026-07-27 cùng nút thêm/bớt nguồn): danh sách
    // nguồn chốt lúc Start và không đổi, nên UI chỉ còn đọc trạng thái và gọi Stop.

    // Ảnh chụp trạng thái cho UI: thread Recv ghi mỗi giây, main thread đọc.
    std::mutex statusMutex;
    std::vector<AgentSourceStatus> statusRows;

    // "Host thắng": một bộ theo dõi dùng chung cho mọi nguồn.
    LocalInputMonitor localInputMon;

    // Máy này trả lời các câu hỏi TRƯỚC KHI có phiên — "đang chia sẻ gì?"
    // (LIST_SOURCES), "còn sống? bao xa?" (PING sessionId=0). Beacon chỉ DỰNG byte
    // trả lời; gửi là việc của vòng Recv, về đúng nơi vừa hỏi. SetSources gọi từ
    // PublishStatus — cùng thread Recv (lần gọi ở Start chạy trước khi thread dựng).
    deskhub::Beacon beacon;

    uint32_t startBitrate = 0, minBitrate = 1'000'000;

    // Địa chỉ nguồn của gói đang xử lý (chỉ thread Recv dùng). Callback `send` của
    // HostSession gửi theo biến này.
    NetAddr replyAddr{};

    void RecvLoop();
    void StartPipeline(SourcePipeline* p);
    void AttachSession(SourcePipeline* p);
    void ShutdownPipeline(SourcePipeline* p);
    SourcePipeline* MakePipeline(const AgentSource& s);
    void PublishStatus();
};

AgentLoop::AgentLoop() = default;

AgentLoop::~AgentLoop() {
    Stop();
}

std::vector<AgentSourceStatus> AgentLoop::Status() {
    if (!impl_) return {};
    std::lock_guard<std::mutex> lk(impl_->statusMutex);
    return impl_->statusRows;
}

SourcePipeline* AgentLoop::Impl::MakePipeline(const AgentSource& s) {
    auto p = std::make_unique<SourcePipeline>(startBitrate, minBitrate);
    p->sourceId = nextSourceId++;
    p->displayId = s.displayId;
    p->name = s.name;
    pipes.push_back(std::move(p));
    return pipes.back().get();
}

// Nối chuỗi capture→encode→gửi cho MỘT nguồn rồi khởi động capture. Tách thành hàm
// vì được gọi ở HAI chỗ: các nguồn ban đầu trong Start, và nguồn thêm giữa phiên khi
// người dùng bấm Add (trong vòng Recv).
void AgentLoop::Impl::StartPipeline(SourcePipeline* p) {
    UdpSocket* sockPtr = &sock;

    // NAL vừa nén xong (thread nội bộ của VideoToolbox, đã nối tiếp hoá) -> cắt gói -> UDP.
    auto onPacket = [p, sockPtr](const uint8_t* data, size_t size, uint64_t tsUs,
                        bool keyframe) {
        if (!p->session || p->session->state() != deskhub::HostSession::State::Streaming) return;
        // Độ trễ THẬT của bộ nén: từ lúc chụp tới lúc NAL ra. Xem dgEncLatSum.
        {
            const uint64_t nowUs = NowUs();
            const uint32_t latMs = nowUs > tsUs ? uint32_t((nowUs - tsUs) / 1000) : 0;
            p->dgEncLatSum.fetch_add(latMs, std::memory_order_relaxed);
            p->dgEncLatCount.fetch_add(1, std::memory_order_relaxed);
            DiagAtomicMax(p->dgEncLatMax, latMs);
        }
        const uint64_t pp = p->peerPacked.load(std::memory_order_acquire);
        if (!pp) return;
        const NetAddr peer = NetAddr::Unpack(pp);
        p->packetizer.SetSessionId(p->session->sessionId());
        // Packetizer là single-thread (luồng này). Thread Recv chỉ đặt ý muốn qua
        // atomic, việc bật/tắt thật diễn ra ở đây — khỏi cần khoá.
        p->packetizer.SetFecEnabled(p->wantFec.load(std::memory_order_relaxed));
        // Đo burst gửi: từ gói đầu tới gói cuối của frame này, và bắt lỗi sendto —
        // buffer gửi đầy là mất gói ngay tại host mà không ai hay.
        const uint64_t sendT0 = NowUs();
        const size_t pkts = p->packetizer.SendFrame(
            std::span<const uint8_t>(data, size), p->nextFrameId++, tsUs, keyframe,
            [p, sockPtr, &peer](std::span<const uint8_t> d) {
                if (sockPtr->SendTo(peer, d.data(), d.size()))
                    p->bytesSent.fetch_add(d.size(), std::memory_order_relaxed);
                else
                    p->dgSendFail.fetch_add(1, std::memory_order_relaxed);
                // GĐ7: giữ bản sao để gửi lại nếu client NACK (Store tự bỏ gói FEC).
                std::lock_guard<std::mutex> lk(p->retxMutex);
                p->retxCache.Store(d);
            });
        const uint32_t burstMs = uint32_t((NowUs() - sendT0) / 1000);
        DiagAtomicMax(p->dgBurstMsMax, burstMs);
        if (pkts) p->framesSent.fetch_add(1, std::memory_order_relaxed);
        // Sự kiện IDR: ghi lại cho thread Recv in — cỡ IDR là con số quyết định chẩn
        // đoán chùm mất gói (docs/06 §7b).
        if (pkts && keyframe) {
            p->dgIdrCount.fetch_add(1, std::memory_order_relaxed);
            p->dgIdrPkts.store(uint32_t(pkts), std::memory_order_relaxed);
            p->dgIdrBurstMs.store(burstMs, std::memory_order_relaxed);
            p->dgIdrBytes.store(uint64_t(size), std::memory_order_release);
        }
    };

    const uint32_t fps = opt.fps;
    p->curFps.store(fps, std::memory_order_relaxed);
    // Tạo encoder nếu chưa có. GỌI DƯỚI encMutex. false = không dựng được session.
    auto ensureEncoder = [p, fps, onPacket](uint32_t w, uint32_t h) -> bool {
        if (p->encoder && p->encoder->IsOpen()) return true;
        EncoderConfig cfg;
        cfg.width = w;
        cfg.height = h;
        // fps của BẬC HIỆN TẠI, không phải trần người dùng: encoder dựng lại sau một
        // lần đổi độ phân giải phải khớp với nhịp thật đang chạy (xem VtEncoder::SetFps
        // về hậu quả của việc lệch).
        cfg.fps = p->curFps.load(std::memory_order_relaxed);
        if (!cfg.fps) cfg.fps = fps;
        cfg.bitrateBps = p->curBitrateBps.load(std::memory_order_relaxed);
        cfg.onPacket = onPacket;
        auto enc = std::make_unique<VtEncoder>();
        if (!enc->Init(cfg)) {
            LOGE("[Agent][%s] VideoToolbox refused to start an encoder.", p->name.c_str());
            p->failed.store(true);
            return false;
        }
        p->encoder = std::move(enc);
        return true;
    };
    p->ensureEncoderFn = ensureEncoder;

    // Đường nóng của nguồn này. CHẠY TRÊN QUEUE CAPTURE của SCStream — không phải
    // thread Recv. Bốn việc, theo thứ tự:
    //   1. Phát hiện đổi kích thước → vứt encoder + cache, báo cho thread Recv.
    //   2. Bỏ frame nếu nguồn nhỏ hơn mức encoder nhận (trạng thái TẠM).
    //   3. Giữ frame cuối làm cache (để còn cái mà encode khi nguồn đứng yên).
    //   4. Encode.
    // Giữ encMutex suốt từ bước 1: thread Recv cũng chạm vào encoder và cachedPb khi
    // nó phải encode lại frame tĩnh lúc client xin IDR.
    auto onFrame = [p, ensureEncoder](const MacFrameInfo& fi) {
        p->captured.fetch_add(1, std::memory_order_relaxed);
        if (p->failed.load()) return;
        // ScreenCapture đã bảo đảm kích thước chẵn; giữ phép & ~1 làm lưới an toàn
        // vì VideoToolbox từ chối kích thước lẻ và lỗi đó rất khó lần.
        const uint32_t encW = fi.width & ~1u, encH = fi.height & ~1u;
        if (!encW || !encH) return;

        std::lock_guard<std::mutex> lk(p->encMutex);

        // Nguồn đổi kích thước (người dùng kéo cửa sổ / đổi độ phân giải màn hình).
        // Encoder và cache đều gắn chặt với kích thước cũ -> vứt cả hai, dựng lại
        // ngay ở frame này. Cờ sizeChanged để thread Recv báo RECONFIG + IDR.
        if (p->srcW.load() != encW || p->srcH.load() != encH) {
            if (p->srcW.load())
                LOGI("[Agent][%s] Source resized %ux%u -> %ux%u, rebuilding encoder.",
                    p->name.c_str(), p->srcW.load(), p->srcH.load(), encW, encH);
            p->srcW.store(encW);
            p->srcH.store(encH);
            p->encoder.reset();
            if (p->cachedPb) {
                CVPixelBufferRelease(static_cast<CVPixelBufferRef>(p->cachedPb));
                p->cachedPb = nullptr;
            }
            p->haveCached.store(false, std::memory_order_release);
            p->sizeChanged.store(true, std::memory_order_release);
        }

        // Nguồn nhỏ hơn mức encoder nhận (cửa sổ vừa bị thu nhỏ). TRẠNG THÁI TẠM,
        // không phải lỗi: bỏ qua frame và giữ nguyên phiên.
        //
        // Chặn ở ĐÚNG chỗ này mới thoát được: trên nó là đoạn ghi nhận kích thước —
        // vẫn phải chạy, vì đó là thứ duy nhất cho ta biết cửa sổ đã mở to trở lại.
        // Dưới nó là cache + encode — đều vô nghĩa ở cỡ này. Thoát sớm hơn (như
        // `failed` làm) là tự bịt mắt: phiên treo vĩnh viễn.
        if (encW < kMinEncodeW || encH < kMinEncodeH) {
            if (!p->paused.exchange(true, std::memory_order_acq_rel))
                LOGI(
                    "[Agent][%s] Source too small to encode (%ux%u) — paused, "
                    "waiting for it to grow back.",
                    p->name.c_str(), encW, encH);
            return;
        }
        if (p->paused.exchange(false, std::memory_order_acq_rel))
            LOGI("[Agent][%s] Source back to %ux%u — resuming.", p->name.c_str(), encW, encH);

        // Giữ frame cuối. RETAIN chứ không chép: buffer thuộc pool của SCStream và
        // chỉ hợp lệ trong callback (CaptureTypes.h), nhưng retain thì nó không bị
        // tái sử dụng. Nhả cái cũ TRƯỚC khi giữ cái mới để pool không cạn.
        if (p->cachedPb) CVPixelBufferRelease(static_cast<CVPixelBufferRef>(p->cachedPb));
        p->cachedPb = CVPixelBufferRetain(static_cast<CVPixelBufferRef>(fi.pixelBuffer));
        p->haveCached.store(p->cachedPb != nullptr, std::memory_order_release);
        p->lastFrameUs.store(fi.timestampUs, std::memory_order_relaxed);

        // Chặn ở đây chứ không sớm hơn: mọi bước trên (cache frame, ghi nhận kích
        // thước) vẫn phải chạy TRƯỚC khi có client, vì Start đang đợi đúng srcW để
        // dựng offer.
        if (!p->netReady.load(std::memory_order_acquire)) return;
        if (!ensureEncoder(encW, encH)) return;
        // Encode liên tục kể cả khi chưa có client (đơn giản, tốc độ bit ổn định);
        // NAL bị bỏ ở onPacket nếu chưa STREAMING.
        p->DiagEncode(p->encoder.get(), fi.pixelBuffer, fi.timestampUs,
            p->forceIdr.exchange(false));
    };

    if (!p->capture.Start(p->displayId, opt.fps, opt.maxDim, onFrame)) {
        LOGE("[Agent][%s] Failed to start capture — skipping this source.", p->name.c_str());
        p->failed.store(true);
    }
}

// Dựng phiên + injector cho MỘT nguồn đã biết kích thước. Cũng được gọi ở hai chỗ:
// các nguồn ban đầu, và nguồn thêm giữa phiên khi frame đầu của nó về.
void AgentLoop::Impl::AttachSession(SourcePipeline* p) {
    p->offer.width = uint16_t(p->srcW.load());
    p->offer.height = uint16_t(p->srcH.load());
    p->offer.fps = uint8_t(opt.fps);
    p->offer.bitrateBps = startBitrate;
    LOGI("[Agent] Source %u \"%s\": %ux%u @%ufps, %u Mbps.", p->sourceId, p->name.c_str(),
        p->offer.width, p->offer.height, opt.fps, opt.bitrateMbps);

    // Chuột/bàn phím LUÔN được chia sẻ (chốt 2026-07-27) — chỉ còn một đường.
    p->injector.SetLocalMonitor(&localInputMon);
    p->injector.SetEnabled(p->injector.Init(p->displayId));

    UdpSocket* sockPtr = &sock;
    Impl* self = this;

    deskhub::HostCallbacks cb;
    cb.send = [sockPtr, self](std::span<const uint8_t> d) {
        sockPtr->SendTo(self->replyAddr, d.data(), d.size());
    };
    // GĐ10: nguồn ngẫu nhiên mã hoá cho nonce challenge, sessionId và token thiết
    // bị. core/ không đụng được API hệ điều hành nên phải nối từ đây. Thiếu callback
    // này thì HostSession từ chối MỌI kết nối (fail closed) — xem BeginSession.
    cb.randomBytes = [](std::span<uint8_t> out) {
        return RandomBytes(out.data(), out.size());
    };
    // Client mới vừa chào và kèm cỡ màn hình của nó. Co luồng cho vừa NGAY BÂY GIỜ,
    // trước khi HELLO_ACK đi ra, rồi sửa lời chào theo cỡ vừa tính — client nhờ vậy
    // dựng bộ giải mã đúng một lần thay vì dựng rồi phải dựng lại sau một RECONFIG.
    //
    // Không đụng tới srcW/srcH ở đây: chúng là cỡ FRAME ĐANG VỀ, và frame cỡ mới còn
    // chưa tới. onFrame sẽ thấy chênh lệch ở frame kế tiếp và đi đúng đường
    // sizeChanged như mọi lần đổi độ phân giải khác — dựng lại encoder, phát RECONFIG.
    const uint32_t maxFps = opt.fps;
    cb.onHello = [p, maxFps](const deskhub::Hello& h) {
        uint32_t w = 0, hgt = 0;
        p->capture.SetClientSize(h.maxWidth, h.maxHeight, w, hgt);
        if (!w || !hgt) return;
        p->offer.width = uint16_t(w);
        p->offer.height = uint16_t(hgt);
        p->offer.bitrateBps = p->curBitrateBps.load(std::memory_order_relaxed);
        p->session->SetOffer(p->offer);
        // Cỡ VỪA chốt xong -> đây là lúc duy nhất biết được TRẦN của thang. Dựng lại
        // mỗi lần có client mới: client khác thì trần khác, và một thang neo vào trần
        // của client trước sẽ co nhầm cỡ.
        p->ladder = std::make_unique<deskhub::QualityLadder>(uint16_t(w), uint16_t(hgt),
            uint8_t(maxFps));
        p->step = p->ladder->current();
        LOGI("[Agent][%s] Quality ladder: ceiling %ux%u @%ufps, %u rung(s).",
            p->name.c_str(), w, hgt, maxFps, p->ladder->rungCount());
    };
    cb.onStart = [p] {
        p->forceIdr.store(true); // IDR mở màn (kèm SPS/PPS — xem VtEncoder.h)
        LOGI("[Agent][%s] Client START — beginning video push.", p->name.c_str());
    };
    cb.onKeyframeRequest = [p] { p->forceIdr.store(true); };
    // GĐ7: client xin gửi lại các mảnh còn thiếu của một frame. Tra kho rồi phát lại
    // thẳng cho peer hiện tại. Rẻ hơn nhiều so với ép cả một IDR, và chỉ tốn băng
    // thông đúng lúc thật sự mất gói. Chạy trên thread Recv.
    cb.onNack = [p, sockPtr](uint32_t frameId, std::span<const uint16_t> indices) {
        const uint64_t pp = p->peerPacked.load(std::memory_order_acquire);
        if (!pp) return;
        const NetAddr peer = NetAddr::Unpack(pp);
        std::lock_guard<std::mutex> lk(p->retxMutex);
        for (uint16_t idx : indices) {
            const auto d = p->retxCache.Find(frameId, idx);
            if (!d.empty()) sockPtr->SendTo(peer, d.data(), d.size());
        }
    };
    cb.onInput = [p](const deskhub::InputEvent& e) { p->injector.Apply(e); };
    // SET_FOCUS(false): client rời nguồn này (đổi màn hình xem / app xuống nền)
    // giữa lúc có thể đang giữ phím — nhả hết, không để kẹt. Nhánh true không còn
    // việc gì: nguồn là cả màn hình, không có cửa sổ nào để kéo lên trước.
    cb.onFocus = [p](bool focused) {
        if (!focused) p->injector.ReleaseAll();
    };
    cb.onDisconnect = [p] {
        p->peerPacked.store(0, std::memory_order_release);
        p->injector.ReleaseAll(); // mất kết nối giữa lúc giữ phím = kẹt phím
        {
            std::lock_guard<std::mutex> lk(p->retxMutex);
            p->retxCache.Reset(); // phiên sau không gửi lại gói của phiên trước
        }
        LOGI("[Agent][%s] Client left (BYE/timeout).", p->name.c_str());
    };
    // GĐ5 congestion control, RIÊNG từng nguồn: hai nguồn có thể đi cùng một đường
    // mạng nhưng bitrate của chúng độc lập, và client có thể chỉ đang xem một trong
    // hai. Policy nằm ở deskhub::BitrateController (core, test được offline); ở đây
    // chỉ còn phần dính thiết bị.
    cb.onFeedback = [p](const deskhub::Feedback& fb) {
        // RTT chỉ đo được ở phía client — FEEDBACK là đường duy nhất nó tới host.
        p->uiRttMs.store(fb.rttMs, std::memory_order_relaxed);
        p->uiLossPct.store(fb.lossPct, std::memory_order_relaxed);
        p->uiRecvKbps.store(fb.recvBitrateKbps, std::memory_order_relaxed);
        p->haveFeedback.store(true, std::memory_order_release);

        const deskhub::BitrateDecision d = p->rate.Update(fb, NowUs());

        if (d.fecToggled) {
            p->wantFec.store(d.fecEnabled, std::memory_order_relaxed);
            LOGI("[Agent][%s] FEC %s (loss %u%%).", p->name.c_str(),
                d.fecEnabled ? "on" : "off", fb.lossPct);
        }

        if (d.changeBitrate) {
            const uint32_t cur = p->rate.bitrateBps();
            std::lock_guard<std::mutex> lk(p->encMutex);
            // Encoder từ chối thì KHÔNG commit: lần Feedback sau tính lại từ mức cũ.
            if (p->encoder && p->encoder->SetBitrate(d.bitrateBps)) {
                p->rate.CommitBitrate(d.bitrateBps);
                p->curBitrateBps.store(d.bitrateBps, std::memory_order_relaxed);
                LOGI("[Agent][%s] Bitrate %.1f -> %.1f Mbps (loss %u%%, RTT %u ms)",
                    p->name.c_str(), cur / 1e6, d.bitrateBps / 1e6, fb.lossPct, fb.rttMs);
            }
        }

        // Bitrate mới chỉ là NGÂN SÁCH. Thang quyết định tiêu ngân sách đó vào đâu —
        // pixel hay khung hình. Chạy MỖI lần Feedback, kể cả khi bitrate không đổi:
        // thang có dwell riêng và cần nhịp đều để đếm, còn changeBitrate thì thưa.
        //
        // Không đụng encoder ở đây. Đổi bậc = đổi cỡ capture (SetQuality) → frame cỡ
        // mới về → onFrame thấy lệch cỡ và đi đúng đường sizeChanged như mọi lần đổi
        // độ phân giải khác, tức là dựng lại encoder + phát RECONFIG + IDR trên thread
        // Recv. Gọi thẳng encoder từ đây là chạm vào luồng khác (docs/06 §4).
        if (!p->ladder) return;
        if (!p->ladder->Update(p->rate.bitrateBps(), NowUs())) return;
        const deskhub::QualityStep prev = p->step;
        p->step = p->ladder->current();
        p->curFps.store(p->step.fps, std::memory_order_relaxed);
        {
            // Encoder đang sống thì nói thẳng fps mới cho nó — bậc chỉ-đổi-fps không
            // dựng lại encoder, nên đây là đường DUY NHẤT con số đó tới được bộ điều
            // khiển tốc độ.
            std::lock_guard<std::mutex> lk(p->encMutex);
            if (p->encoder) p->encoder->SetFps(p->step.fps);
        }
        uint32_t w = 0, hgt = 0;
        p->capture.SetQuality(p->step.scalePct, p->step.fps, w, hgt);
        LOGI("[Agent][%s] Quality %u%%@%ufps -> %u%%@%ufps (%ux%u, budget %.1f Mbps)",
            p->name.c_str(), prev.scalePct, prev.fps, p->step.scalePct, p->step.fps, w, hgt,
            p->rate.bitrateBps() / 1e6);
        // Bậc chỉ đổi fps thì cỡ buffer y nguyên, nên onFrame KHÔNG thấy gì và không
        // có RECONFIG nào được phát — mà client thì bắt buộc phải biết fps mới
        // (deskhub::Reconfig::fps). Đặt cờ để vòng Recv phát RECONFIG bất kể cỡ.
        p->qualityChanged.store(true, std::memory_order_release);
    };

    p->session = std::make_unique<deskhub::HostSession>(cb, p->offer);
    p->netReady.store(true, std::memory_order_release);
}

// Tắt hẳn MỘT nguồn: nút Stop, nguồn thêm vào mà không lên hình, hoặc dọn dẹp cuối
// phiên. Idempotent (shutdownDone). CHỈ gọi từ thread Recv.
void AgentLoop::Impl::ShutdownPipeline(SourcePipeline* p) {
    if (p->shutdownDone) return;
    p->shutdownDone = true;
    p->injector.ReleaseAll(); // tắt giữa lúc client đang giữ phím -> nhả ra
    // Chia tay tử tế: báo BYE cho client nếu còn phiên.
    if (p->session && p->session->state() != deskhub::HostSession::State::Idle) {
        const uint64_t pp = p->peerPacked.load();
        if (pp) {
            uint8_t bye[deskhub::kCommonHeaderSize];
            const size_t bn = deskhub::BuildBye(bye, p->session->sessionId());
            if (bn) sock.SendTo(NetAddr::Unpack(pp), bye, bn);
        }
    }
    p->capture.Stop(); // hết callback rồi mới dọn encoder
    {
        std::lock_guard<std::mutex> lk(p->encMutex);
        if (p->encoder) p->encoder->Finish();
        if (p->cachedPb) {
            CVPixelBufferRelease(static_cast<CVPixelBufferRef>(p->cachedPb));
            p->cachedPb = nullptr;
            p->haveCached.store(false, std::memory_order_release);
        }
    }
    p->netReady.store(false);
    p->failed.store(true); // mọi vòng lặp coi nguồn này như đã chết
}

// Đẩy ảnh chụp trạng thái cho UI. Gọi mỗi giây từ thread Recv (và một lần lúc Start).
void AgentLoop::Impl::PublishStatus() {
    std::vector<AgentSourceStatus> rows;
    std::vector<deskhub::SourceInfo> infos; // ảnh chụp cho Beacon, dựng cùng lượt
    for (SourcePipeline* p : live) {
        if (p->failed.load() || p->capture.Closed()) continue;
        const uint64_t peer = p->peerPacked.load(std::memory_order_relaxed);
        AgentSourceStatus r;
        r.sourceId = p->sourceId;
        r.name = p->name;
        r.width = p->srcW.load();
        r.height = p->srcH.load();
        r.viewerConnected = peer != 0;
        if (peer) r.viewerAddr = NetAddr::Unpack(peer).ToString();
        r.captureFps = p->statCaptureFps;
        r.sendFps = p->statSendFps;
        r.sendKbps = p->statSendKbps;
        r.rttMs = p->uiRttMs.load(std::memory_order_relaxed);
        rows.push_back(std::move(r));

        deskhub::SourceInfo si;
        si.sourceId = p->sourceId;
        si.width = uint16_t(p->srcW.load());
        si.height = uint16_t(p->srcH.load());
        si.name = p->name;
        infos.push_back(std::move(si));
    }
    // Beacon trả lời trên CÙNG thread Recv này nên cập nhật thẳng, không khoá.
    beacon.SetSources(infos);

    std::lock_guard<std::mutex> lk(statusMutex);
    statusRows = std::move(rows);
}

// ---------------------------------------------------------------------------
// Start — sáu giai đoạn, đánh dấu bằng các mốc "--- ... ---" bên dưới
// ---------------------------------------------------------------------------
//   1. Kiểm tra đầu vào + quyền, mở socket.
//   2. Dựng SourcePipeline cho từng nguồn.
//   3. Khởi động capture — từ đây các queue frame bắt đầu chạy.
//   4. ĐỢI frame đầu của từng nguồn: phải biết kích thước thật rồi mới chào được
//      trong HELLO_ACK. Nguồn không phát frame nào trong 10 giây thì bỏ, không kéo
//      cả phiên xuống theo.
//   5. Dựng HostSession + InputInjector cho từng nguồn còn sống.
//   6. Dựng thread Recv rồi trả về (khác bản Windows — xem AgentLoop.h).
bool AgentLoop::Start(const std::vector<AgentSource>& sources, const AgentOptions& opt) {
    Stop();
    impl_ = std::make_unique<Impl>();
    Impl* im = impl_.get();
    im->opt = opt;

    if (sources.empty()) {
        LOGE("[Agent] No display to share.");
        return false;
    }
    if (sources.size() > deskhub::kMaxSources) {
        LOGE("[Agent] At most %zu sources can be shared at once.", deskhub::kMaxSources);
        return false;
    }
    if (!macperm::HasScreenRecording()) {
        // Không chặn hẳn: quyền có thể vừa được cấp và preflight còn cache giá trị
        // cũ. Nhưng nói ra để log giải thích được ca "danh sách nguồn trống rỗng".
        LOGW(
            "[Agent] Screen Recording permission not detected — "
            "capture will likely fail. Grant it in System Settings and restart.");
    }

    // MỘT cổng duy nhất, không có phương án hai (giống bản Windows). Bản trước dò 64
    // cổng kế tiếp khi 47777 bận; việc đó đã bỏ vì client chỉ biết gõ IP — một host
    // nhảy sang 47778 là một host không ai kết nối tới được.
    if (!im->sock.Open(kDeskhubPort)) {
        LOGE(
            "[Agent] UDP port %u is not available — another Deskhub is probably "
            "still running. Close it and try again.",
            unsigned(kDeskhubPort));
        return false;
    }
    im->sock.SetRecvTimeout(100);

    im->startBitrate = opt.bitrateMbps * 1'000'000u;
    // Sàn bitrate: dưới mức này hình nát tới mức vô dụng, thà bỏ frame còn hơn. Đây
    // là tham số `minBps` của BitrateController — nó không bao giờ tụt quá đây.
    im->minBitrate = 1'000'000u;

    LOGI("[Agent] Listening on UDP port %u. On the other machine, enter one of:",
        unsigned(kDeskhubPort));
    for (const auto& a : ListLocalIPv4()) LOGI("    %s    (%s)", a.ip.c_str(), a.name.c_str());

    // --- Dựng + khởi động capture cho từng nguồn ---
    for (const AgentSource& s : sources) im->StartPipeline(im->MakePipeline(s));

    // --- Đợi frame đầu của từng nguồn để biết kích thước (offer trong HELLO_ACK) ---
    for (int i = 0; i < 1000; ++i) {
        bool allKnown = true;
        for (auto& p : im->pipes)
            if (!p->failed.load() && !p->srcW.load() && !p->capture.Closed()) allKnown = false;
        if (allKnown) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Nguồn không phát frame nào trong 10s thì bỏ, không kéo cả phiên xuống theo.
    for (auto& p : im->pipes) {
        if (p->failed.load() || !p->srcW.load()) {
            if (!p->failed.load())
                LOGW("[Agent][%s] No frame within 10s — not sharing this source.",
                    p->name.c_str());
            im->ShutdownPipeline(p.get());
            continue;
        }
        im->live.push_back(p.get());
    }
    if (im->live.empty()) {
        LOGE("[Agent] No usable source — stopping.");
        im->sock.Close();
        return false;
    }

    for (SourcePipeline* p : im->live) im->AttachSession(p);

    // "Host thắng" khi hai bên cùng điều khiển. Luôn cần: input luôn được chia sẻ.
    {
        im->localInputMon.Start();
        const bool ax = macperm::HasAccessibility();
        LOGI("[Agent] Client control allowed (mouse + keyboard). Accessibility: %s%s",
            ax ? "YES" : "NO",
            ax ? "" : " — input will be silently dropped until it is granted");
    }
    LOGI("[Agent] Sharing %zu source(s). Waiting for client...", im->live.size());

    im->PublishStatus();
    running_.store(true, std::memory_order_release);
    im->recvThread = std::thread([this, im] {
        im->RecvLoop();
        // Vòng Recv tự thoát (lỗi socket / hết nguồn sống) — hạ cờ để UI poll thấy
        // phiên đã kết thúc, giống RunAgent bên Windows trả về. An toàn: Stop()
        // join thread này trước khi impl_ và AgentLoop bị huỷ.
        running_.store(false, std::memory_order_release);
    });
    return true;
}

void AgentLoop::Stop() {
    if (!impl_) return;
    Impl* im = impl_.get();
    im->quit.store(true);
    if (im->recvThread.joinable()) im->recvThread.join();
    running_.store(false, std::memory_order_release);

    im->localInputMon.Stop();

    // Quét TOÀN BỘ pipes chứ không riêng live: nguồn đang chờ frame đầu và nguồn đã
    // tắt giữa phiên đều nằm ngoài live; ShutdownPipeline idempotent nên nguồn nào
    // dọn rồi chỉ là no-op, còn tổng kết thì tính cả nguồn đã tắt giữa chừng.
    uint64_t totalFrames = 0;
    double totalMB = 0;
    for (auto& up : im->pipes) {
        im->ShutdownPipeline(up.get());
        totalFrames += up->framesSent.load();
        totalMB += up->bytesSent.load() / 1e6;
    }
    im->sock.Close();
    LOGI("[Agent] Stopped. Total: %" PRIu64 " frames sent, %.2f MB.", totalFrames, totalMB);
    impl_.reset();
}

// ---------------------------------------------------------------------------
// Vòng Recv (thread riêng), dùng chung cho mọi nguồn
// ---------------------------------------------------------------------------
// Mỗi vòng làm ba việc, theo thứ tự:
//   1. recvfrom — chặn tối đa 100 ms, nên vòng lặp luôn quay đủ nhanh để Tick.
//   2. Định tuyến gói vừa nhận về đúng SourcePipeline (xem sơ đồ đầu file).
//   3. Tick mọi phiên + thống kê mỗi giây.
//
// Bản trước còn một bước "thi hành lệnh từ UI (Add / Stop selected)" đứng trước cả
// ba; nó đã bỏ 2026-07-27 — danh sách nguồn chốt lúc Start và không đổi.
void AgentLoop::Impl::RecvLoop() {
    uint8_t buf[deskhub::kMaxDatagram];
    uint8_t beaconBuf[deskhub::kMaxDatagram]; // riêng, không dùng chung với buf nhận:
                                              // Reply() đọc gói đến trong lúc ghi ra
    uint64_t lastStatUs = NowUs();
    // Thời gian BẬN dài nhất của một vòng Recv trong cửa sổ 1s (không tính lúc chờ
    // recvfrom). Vòng này mà nghẽn thì buffer UDP của kernel gánh — tràn là mất gói
    // thật. Chỉ thread Recv chạm nên không cần atomic.
    uint32_t dgLoopBusyMaxMs = 0;

    while (!quit.load()) {
        // Hết nguồn sống là hết phiên (giống bản Windows): một phiên không còn màn
        // hình nào chỉ là một cửa sổ trống — UI thấy running() tắt và thu dọn.
        bool anyAlive = false;
        for (SourcePipeline* p : live)
            if (!p->failed.load() && !p->capture.Closed()) anyAlive = true;
        if (!anyAlive) {
            LOGI("[Agent] No source left alive — session over.");
            break;
        }

        NetAddr from;
        const int n = sock.RecvFrom(buf, sizeof(buf), from);
        const uint64_t now = NowUs();
        if (n < 0) {
            LOGE("[Agent] Socket error — stopping.");
            break;
        }

        if (n > 0) {
            const auto span = std::span<const uint8_t>(buf, size_t(n));
            const auto h = deskhub::ParseCommonHeader(span);
            // Hỏi-đáp KHÔNG thuộc phiên nào (LIST_SOURCES, PING dò đường) đi trước
            // và trả lời về ĐÚNG `from` — chúng tới từ địa chỉ bất kỳ trong mạng,
            // không phải peer của phiên. Beacon dựng byte, ta gửi.
            //
            // Đặt TRƯỚC `replyAddr = from` là có chủ ý: replyAddr là nơi callback
            // `send` của mọi phiên gửi tới, nên một máy lạ dò đường không được phép
            // chiếm chỗ client thật ở đó. Xem deskhub/session/Beacon.h.
            if (const size_t rn = beacon.Reply(beaconBuf, span); rn) {
                sock.SendTo(from, beaconBuf, rn);
            } else if (h) {
                replyAddr = from;
                // HELLO chưa có sessionId -> định tuyến theo sourceId. Mọi gói khác
                // đã mang sessionId -> tìm phiên khớp.
                SourcePipeline* dst = nullptr;
                if (h->type == deskhub::MsgType::Hello) {
                    const auto m = deskhub::ParseHello(deskhub::PayloadOf(span));
                    if (m)
                        for (SourcePipeline* p : live)
                            if (p->sourceId == m->sourceId) dst = p;
                } else if (h->sessionId) {
                    for (SourcePipeline* p : live)
                        if (p->session && p->session->sessionId() == h->sessionId) dst = p;
                }
                if (dst && !dst->failed.load() && dst->session->HandlePacket(span, now)) {
                    // Gói hợp lệ thuộc phiên — cập nhật peer (client đổi IP/port).
                    const uint64_t pk = from.Pack();
                    if (dst->peerPacked.load(std::memory_order_relaxed) != pk) {
                        dst->peerPacked.store(pk, std::memory_order_release);
                        LOGI("[Agent][%s] Peer: %s", dst->name.c_str(),
                            from.ToString().c_str());
                    }
                }
            }
        }

        for (SourcePipeline* p : live) {
            if (p->failed.load()) continue;
            // Nguồn biến mất giữa phiên (cửa sổ đóng) — dọn ngay thay vì để nó im
            // lặng mãi và người xem ngồi nhìn khung đứng hình.
            if (p->capture.Closed() && !p->shutdownDone) {
                LOGI("[Agent][%s] Source closed — stopping this source.", p->name.c_str());
                ShutdownPipeline(p);
                PublishStatus();
                continue;
            }
            p->session->Tick(now);

            // Sự kiện IDR do luồng nén ghi lại — in ở đây để I/O không nằm trên
            // đường nóng. Luôn bật: IDR hiếm và cỡ của nó là con số chẩn đoán quan
            // trọng nhất phía host.
            if (const uint64_t ib = p->dgIdrBytes.exchange(0, std::memory_order_acquire)) {
                LOGI("[DIAG][%s] evt=idr bytes=%" PRIu64 " pkts=%u burst_ms=%u",
                    p->name.c_str(), ib, p->dgIdrPkts.load(std::memory_order_relaxed),
                    p->dgIdrBurstMs.load(std::memory_order_relaxed));
            }

            // Nguồn vừa đổi kích thước (luồng capture đã vứt encoder). Báo client
            // kích thước mới + IDR: stream đổi SPS giữa chừng, không có IDR thì
            // decoder client chỉ có rác cho tới keyframe kế tiếp.
            // `paused` đứng TRƯỚC exchange là cố ý: short-circuit giữ nguyên cờ
            // sizeChanged trong lúc tạm dừng, nên client không phải dựng lại decoder
            // cho một cỡ suy biến rồi lát nữa dựng lại lần nữa.
            const bool sized = p->sizeChanged.exchange(false, std::memory_order_acq_rel);
            const bool qual = p->qualityChanged.exchange(false, std::memory_order_acq_rel);
            if (!p->paused.load(std::memory_order_acquire) && (sized || qual)) {
                p->offer.width = uint16_t(p->srcW.load());
                p->offer.height = uint16_t(p->srcH.load());
                p->offer.fps = uint8_t(p->step.fps ? p->step.fps : opt.fps);
                p->offer.bitrateBps = p->curBitrateBps.load(std::memory_order_relaxed);
                p->session->SetOffer(p->offer); // HELLO phát lại sau phải mang số mới
                const uint64_t pp = p->peerPacked.load(std::memory_order_acquire);
                if (pp && p->session->state() == deskhub::HostSession::State::Streaming) {
                    deskhub::Reconfig rc{p->offer.width, p->offer.height, p->offer.bitrateBps,
                        p->offer.fps};
                    uint8_t rbuf[deskhub::kMaxDatagram];
                    const size_t rn = deskhub::BuildReconfig(rbuf, p->session->sessionId(), rc);
                    if (rn) sock.SendTo(NetAddr::Unpack(pp), rbuf, rn);
                    // IDR chỉ cần khi CỠ đổi (SPS mới, decoder client dựng lại). Bậc
                    // chỉ-đổi-fps không đụng SPS — ép IDR ở đó là tự bắn một frame nặng
                    // gấp hàng chục lần vào đúng đường truyền vừa báo là đang yếu.
                    if (sized) p->forceIdr.store(true);
                }
            }

            // Nguồn đang TĨNH (SCStream chỉ phát frame khi nội dung đổi) — encoder
            // không có input mới. Hai nhu cầu gộp chung một chỗ:
            //   1. Yêu cầu IDR đang treo (>200ms không có frame) → encode lại frame
            //      cache với IDR, không thì client vào xem màn hình tĩnh sẽ đen mãi.
            //   2. KEEPALIVE ~2fps: giữ đồng hồ trình bày của client chạy và đẩy nốt
            //      frame còn nằm trong encoder. Nội dung không đổi nên P-frame chỉ
            //      vài KB, chi phí không đáng kể.
            const uint64_t sinceFrameUs = now - p->lastFrameUs.load(std::memory_order_relaxed);
            const bool wantIdrFlush = p->forceIdr.load() && sinceFrameUs > 200'000;
            const bool wantKeepalive = sinceFrameUs > 500'000 &&
                                       now - p->lastKeepaliveUs >= 500'000;
            if (p->session->state() == deskhub::HostSession::State::Streaming &&
                p->haveCached.load(std::memory_order_acquire) &&
                (wantIdrFlush || wantKeepalive)) {
                std::lock_guard<std::mutex> lk(p->encMutex);
                if (p->cachedPb &&
                    p->ensureEncoderFn(p->srcW.load(), p->srcH.load())) {
                    // Timestamp MỚI chứ không dùng lại mốc của frame cũ: client tính
                    // e2e từ trường này, và bơm lại một mốc cũ làm nó báo trễ hàng
                    // chục giây (đúng lỗi bản Windows gặp ngày 21/07/2026).
                    p->DiagEncode(p->encoder.get(), p->cachedPb, now,
                        p->forceIdr.exchange(false));
                    // ...rồi ÉP nó nhả ra ngay. Đặt mốc mới thôi chưa đủ: ở nhịp
                    // 2fps VideoToolbox ngậm frame chờ thêm đầu vào và nhả chậm hơn
                    // ta bơm, nên độ trễ tích luỹ dần — client đo được 5,6 GIÂY trên
                    // một màn hình đứng yên (log 30/07/2026). Xem VtEncoder::Flush.
                    p->encoder->Flush();
                    p->lastKeepaliveUs = now;
                }
            }
        }

        if (now - lastStatUs >= 1'000'000) {
            const double secs = (now - lastStatUs) / 1e6;
            for (SourcePipeline* p : live) {
                if (p->failed.load()) continue;
                const uint32_t cap = p->captured.load();
                const uint64_t by = p->bytesSent.load(), fr = p->framesSent.load();
                const auto& ist = p->session->inputStats();
                p->statCaptureFps = (cap - p->lastCaptured) / secs;
                p->statSendFps = (fr - p->lastFrames) / secs;
                p->statSendKbps = (by - p->lastBytes) * 8.0 / 1000.0 / secs;
                // `applied` là thống kê MẠNG (event tới nơi và được giao cho
                // injector), KHÔNG phải bằng chứng phím đã tới ứng dụng. Injector còn
                // vứt tiếp ở cổng tiêu điểm — `skipped` là con số duy nhất lộ ra
                // chuyện đó. Thiếu nó thì "gõ không ăn" không phân biệt được với
                // "không nhận được gói".
                // Nửa sau của dòng là SỐ LIỆU CỦA CLIENT (từ FEEDBACK, ~1s/lần), tức
                // thứ duy nhất host biết về đầu kia. In "-" khi chưa có feedback nào
                // để không nhầm "chưa nghe được gì" với "0% mất gói, RTT 0".
                char link[64] = " | client -";
                if (p->haveFeedback.load(std::memory_order_acquire))
                    std::snprintf(link, sizeof(link),
                        " | client loss %u%%, RTT %u ms, recv %u kbps",
                        p->uiLossPct.load(std::memory_order_relaxed),
                        p->uiRttMs.load(std::memory_order_relaxed),
                        p->uiRecvKbps.load(std::memory_order_relaxed));
                LOGI(
                    "[Agent t=%s][%s] %-9s | capture %.0f fps | send %.0f fps, %.0f kbps"
                    " | input %" PRIu64 " (lost %" PRIu64 ", skipped %" PRIu64 ")%s",
                    deskhubp::LocalTimeHms().c_str(), p->name.c_str(), StateName(p->session->state()),
                    p->statCaptureFps, p->statSendFps, p->statSendKbps,
                    ist.applied, ist.lost, p->injector.skipped(), link);
                p->lastCaptured = cap;
                p->lastBytes = by;
                p->lastFrames = fr;

                const uint32_t ec = p->dgEncCount.exchange(0, std::memory_order_relaxed);
                const uint32_t es = p->dgEncMsSum.exchange(0, std::memory_order_relaxed);
                const uint32_t em = p->dgEncMsMax.exchange(0, std::memory_order_relaxed);
                const uint32_t lc = p->dgEncLatCount.exchange(0, std::memory_order_relaxed);
                const uint32_t ls = p->dgEncLatSum.exchange(0, std::memory_order_relaxed);
                LOGI(
                    "[DIAG][%s] evt=sum enc_ms_avg=%.1f enc_ms_max=%u"
                    " enc_lat_ms=%.1f/%u cap_idle=%u idr=%u"
                    " burst_ms_max=%u send_fail=%u",
                    p->name.c_str(), ec ? double(es) / ec : 0.0, em,
                    lc ? double(ls) / lc : 0.0,
                    p->dgEncLatMax.exchange(0, std::memory_order_relaxed),
                    p->capture.TakeIdleCount(),
                    p->dgIdrCount.exchange(0, std::memory_order_relaxed),
                    p->dgBurstMsMax.exchange(0, std::memory_order_relaxed),
                    p->dgSendFail.exchange(0, std::memory_order_relaxed));
            }
            LOGI("[DIAG][agent] evt=sum loop_busy_ms_max=%u", dgLoopBusyMaxMs);
            dgLoopBusyMaxMs = 0;
            PublishStatus();
            lastStatUs = now;
        }

        // Vòng này bận bao lâu (từ lúc recvfrom trả về tới đây). Nghẽn nặng thì báo
        // ngay, không đợi cửa sổ 1s.
        const uint32_t busyMs = uint32_t((NowUs() - now) / 1000);
        if (busyMs > dgLoopBusyMaxMs) dgLoopBusyMaxMs = busyMs;
        if (busyMs > 250) LOGW("[DIAG][agent] evt=recv_stall busy_ms=%u", busyMs);
    }
}
