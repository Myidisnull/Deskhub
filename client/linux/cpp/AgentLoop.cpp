// =============================================================================
// AgentLoop.cpp — vai trò HOST. Nơi mọi thứ của phía chia sẻ được ghép lại.
//                 Port của client/macos/app/cpp/AgentLoop.cpp; ba backend phần
//                 cứng đổi (PipeWire / VA-API / uinput), phần điều phối giữ
//                 nguyên từng bước.
//
// NHIỆM VỤ
//   Ghép chuỗi bắt hình/mã hoá với tầng mạng, rồi nhân lên cho nhiều nguồn. Đây là
//   file điều phối lớn nhất phía host — bản thân nó không cài đặt thuật toán nào,
//   mà nối các mảnh đã có và quản lý luồng giữa chúng.
//
// ⚠ KIẾN TRÚC LUỒNG — điều quan trọng nhất phải nắm trước khi sửa
//
//   MỖI NGUỒN có một thread PipeWire riêng (do pw_thread_loop tạo):
//       capture → encoder.Encode() → onPacket → Packetizer → sock
//   KHÁC BẢN macOS Ở ĐÂY: VideoToolbox nén BẤT ĐỒNG BỘ trên thread riêng của nó,
//   còn VA-API nén ĐỒNG BỘ — nên onPacket chạy ngay trên thread PipeWire này.
//   Packetizer (single-thread, không tự khoá) vẫn an toàn vì mọi lời gọi Encode
//   đều nằm dưới encMutex (xem encode/VaEncoder.h).
//
//   MỘT thread Recv DÙNG CHUNG cho mọi nguồn (do AgentLoop::Start tạo):
//       recvfrom (timeout 100ms) → định tuyến gói → Tick mọi phiên → thống kê 1s/lần
//
//   Nghĩa là với N nguồn thì có N thread capture + 1 thread Recv, và mọi trạng
//   thái đi qua ranh giới giữa chúng phải là atomic hoặc được mutex bảo vệ.
//   SourcePipeline bên dưới ghi rõ từng trường thuộc về thread nào — ĐỌC PHẦN ĐÓ
//   trước khi thêm trường mới.
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
// HAI CƠ CHẾ ĐÁNG CHÚ Ý (giữ nguyên từ bản Windows/macOS, vì lý do y hệt)
//   1. forceIdr là ATOMIC FLAG. Đặt từ thread Recv (onStart / onKeyframeRequest),
//      tiêu thụ ở lần Encode kế tiếp trên thread capture. Không gọi thẳng encoder
//      từ thread Recv được — nó thuộc luồng kia (docs/06 §4).
//   2. NÉN LẠI FRAME CUỐI. Compositor chỉ phát frame khi nội dung ĐỔI. Nguồn đang
//      tĩnh mà client xin IDR thì không có frame nào để nén — không xử lý thì
//      client vào xem màn hình tĩnh sẽ đen VĨNH VIỄN. Bản Windows/macOS giữ một
//      bản sao frame cuối; ở đây RẺ HƠN NHIỀU: surface NV12 đầu ra của VPP vẫn
//      còn nguyên trong VRAM, chỉ cần gọi VaEncoder::EncodeLast (xem VaEncoder.h).
//      Vì thế SourcePipeline ở đây KHÔNG có trường cachedPb như bản macOS.
//
// LIÊN QUAN: AgentLoop.h (AgentSource/AgentOptions/AgentSourceStatus),
//            ClientLoop.cpp (phía đối diện), deskhub/session/HostSession.h,
//            deskhub/transport/Packetizer.h, client/macos/app/cpp/AgentLoop.cpp,
//            docs/06-transport.md §4, docs/17-linux-app.md
// =============================================================================
#include "AgentLoop.h"

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <functional>
#include <utility>

#include "Log.h"
#include "capture/PortalScreenCast.h"
#include "capture/ScreenCapture.h"
#include "deskhubp/Clock.h"
#include "deskhubp/LogFile.h" // LocalTimeHms — đóng dấu giờ dòng mỗi giây
#include "deskhubp/Random.h"
#include "encode/VaEncoder.h"
#include "input/InputInjector.h"
#include "input/LocalInputMonitor.h"
#include "net/NetInfo.h"
#include "net/UdpSocket.h"

#include "deskhub/control/BitrateController.h"
#include "deskhub/control/QualityLadder.h"
#include "deskhub/control/StreamSize.h"
#include "deskhub/session/Beacon.h" // trả lời LIST_SOURCES / PING dò trước phiên
#include "deskhub/session/HostSession.h"
#include "deskhub/transport/Packetizer.h"
#include "deskhub/transport/RetransmitCache.h"

namespace {

// Cập nhật "giá trị lớn nhất từng thấy" trên một atomic. Vòng compare_exchange là
// cách chuẩn: đọc-so-ghi phải nguyên tử, không thì hai thread cùng ghi sẽ nuốt mất
// một mẫu. (Đối ứng DiagAtomicMax trong client/windows/cpp/Diag.h.)
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

// Cỡ khung nhỏ nhất còn encode được. Bộ mã hoá phần cứng từ chối khung quá nhỏ.
// Trên Linux ca gặp thật là đổi độ phân giải màn hình giữa phiên.
inline constexpr uint32_t kMinEncodeW = 160;
inline constexpr uint32_t kMinEncodeH = 64;

// Toàn bộ trạng thái của MỘT nguồn. Chứa mutex/atomic nên không copy/move được —
// giữ trong vector<unique_ptr>.
struct SourcePipeline {
    SourcePipeline(uint32_t startBps, uint32_t minBps)
        : curBitrateBps(startBps), rate(startBps, minBps) {}

    // --- Cấu hình, cố định sau khi dựng ---
    uint8_t sourceId = 0;
    uint32_t nodeId = 0;
    std::string name;
    int32_t srcX = 0, srcY = 0;

    ScreenCapture capture;
    InputInjector injector;                        // chỉ thread Recv chạm
    std::unique_ptr<deskhub::HostSession> session; // tạo sau khi biết kích thước nguồn
    deskhub::StreamParams offer;                   // chỉ thread Recv chạm
    deskhub::Packetizer packetizer;                // chỉ thread capture chạm

    // GĐ7 NACK: kho các datagram video vừa phát, để gửi lại khi client xin. Store
    // từ thread capture (đường phát), Find từ thread Recv (xử lý NACK) → khoá chung.
    deskhub::RetransmitCache retxCache;
    std::mutex retxMutex;

    // --- Chia sẻ giữa thread capture và thread Recv ---
    std::atomic<uint32_t> srcW{0}, srcH{0}; // cỡ NÉN (đã qua trần + bậc thang)
    // Cỡ NATIVE của stream PipeWire. Tách khỏi srcW/srcH vì hai cái đó là cỡ nén:
    // gộp một biến thì màn 3840 co xuống 1920 sẽ trông như "vừa đổi độ phân giải"
    // ở mỗi frame và encoder bị dựng lại vĩnh viễn.
    std::atomic<uint32_t> nativeW{0}, nativeH{0};
    // Cỡ MUỐN nén (trần người dùng + trần màn client + bậc thang). Recv ghi,
    // PipeWire đọc. 0 = chưa tính, frame đầu tự tính lấy từ trần người dùng.
    std::atomic<uint32_t> wantW{0}, wantH{0};
    // fps của bậc đang chạy, cho ensureEncoder trên thread PipeWire.
    std::atomic<uint32_t> curFps{0};
    // Mốc frame gần nhất ĐÃ NÉN — cổng nhịp của thang đọc/ghi. Chỉ thread PipeWire.
    std::atomic<uint64_t> lastEncodeUs{0};
    // Thang vừa đổi bậc (kể cả khi chỉ đổi fps, tức cỡ không đổi). Recv đặt và tiêu thụ.
    std::atomic<bool> qualityChanged{false};
    std::atomic<bool> sizeChanged{false};
    std::atomic<bool> wantFec{false};
    std::atomic<uint32_t> curBitrateBps{0};
    std::atomic<bool> netReady{false};
    // failed = HỎNG THẬT, một chiều: capture không start được, nguồn biến mất.
    // Nguồn coi như chết tới hết phiên.
    std::atomic<bool> failed{false};
    // Đã tắt hẳn (người dùng bấm Stop, hoặc dọn cuối phiên). Chỉ thread Recv chạm —
    // để ShutdownPipeline idempotent, gọi lại lần hai là no-op.
    bool shutdownDone = false;
    // paused = TẠM không encode được (nguồn nhỏ hơn kMinEncode*), HAI CHIỀU. Tách
    // khỏi `failed` vì gộp chung thì màn hình bị đổi độ phân giải xuống mức thấp
    // sẽ giết phiên vĩnh viễn: onFrame thoát ngay ở đầu hàm nên không bao giờ thấy
    // nó trở lại (bài học 21/07/2026 của bản Windows).
    std::atomic<bool> paused{false};
    std::atomic<bool> forceIdr{false};
    std::atomic<uint64_t> peerPacked{0}; // NetAddr::Pack của client hiện tại (0 = chưa có)
    std::atomic<uint64_t> bytesSent{0}, framesSent{0};
    std::atomic<uint32_t> captured{0};
    std::atomic<uint32_t> nextFrameId{0};
    // RTT chỉ đo được ở PHÍA CLIENT (nó phát PING và trừ khi PONG về), nên FEEDBACK
    // là đường duy nhất con số đó tới được host — UI cần nó để hiện "máy đang xem
    // cách bao xa".
    std::atomic<uint32_t> uiRttMs{0};

    std::mutex encMutex; // bảo vệ encoder giữa hai luồng
    std::unique_ptr<VaEncoder> encoder;
    // Dựng encoder theo kích thước hiện tại. Thread Recv cũng cần gọi (nén lại
    // frame tĩnh khi có yêu cầu IDR) nên phải giữ được sau khi vòng khởi tạo kết
    // thúc. GỌI DƯỚI encMutex.
    std::function<bool(uint32_t, uint32_t)> ensureEncoderFn;

    // Cỡ màn hình client (HELLO) và thang chất lượng. Chỉ thread Recv chạm.
    uint32_t cliW = 0, cliH = 0;
    std::unique_ptr<deskhub::QualityLadder> ladder;
    deskhub::QualityStep step;

    std::atomic<uint64_t> lastFrameUs{0};
    uint64_t lastKeepaliveUs = 0; // chỉ thread Recv chạm

    // --- Congestion control, chỉ thread Recv chạm ---
    // Policy thuần ở core; curBitrateBps/wantFec ở trên là bản sao atomic cho
    // thread capture đọc (nó không được chạm vào rate).
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
    // Sự kiện IDR gần nhất — thread capture ghi, thread Recv in (giữ I/O ngoài
    // đường nóng, bài học Pacer ở docs/06 §7b). bytes==0 = không có sự kiện; ghi
    // bytes CUỐI CÙNG với release để hai trường kia nhìn thấy trước nó.
    // ⚠ ĐỘ TRỄ THẬT CỦA BỘ NÉN — khoảng mù lớn nhất của toàn bộ chuỗi đo (thêm
    //   30/07/2026). `enc_ms` KHÔNG phải cái này: nó chỉ đo thời gian NỘP frame, mà
    //   bộ nén nén BẤT ĐỒNG BỘ. Frame nằm trong đường ống encoder bao lâu trước khi
    //   ra thành NAL thì trước nay không ai đếm.
    //   Đây là con số quyết định: đường ống sâu 4 frame ở 60fps là 67 ms độ trễ HẰNG
    //   SỐ — và hằng số thì e2e phía client (bộ lọc min, deskhub/control/ClockOffset.h)
    //   trừ mất sạch. Máy đo báo 7 ms mà người dùng thấy lag chính là ca đó.
    std::atomic<uint32_t> dgEncLatSum{0}, dgEncLatMax{0}, dgEncLatCount{0};
    std::atomic<uint64_t> dgIdrBytes{0};
    std::atomic<uint32_t> dgIdrPkts{0}, dgIdrBurstMs{0};

    // Đo thời gian một lần Encode + cộng vào bộ đếm cửa sổ. Gọi từ CẢ HAI luồng.
    // Khác bản macOS: VA-API nén ĐỒNG BỘ nên con số này là thời gian nén THẬT,
    // không phải chỉ thời gian nộp frame — nó là chỉ báo trực tiếp GPU có theo kịp
    // fps không.
    void DiagEncode(const std::function<bool()>& doEncode, bool idr) {
        const uint64_t t0 = NowUs();
        const bool ok = doEncode();
        const uint32_t ms = uint32_t((NowUs() - t0) / 1000);
        dgEncMsSum.fetch_add(ms, std::memory_order_relaxed);
        dgEncCount.fetch_add(1, std::memory_order_relaxed);
        DiagAtomicMax(dgEncMsMax, ms);
        // Encode hỏng trên đường keepalive/IDR tĩnh mà nuốt im lặng thì nguồn tĩnh
        // + encoder chết = client trắng hình không dấu vết.
        if (!ok) LOGW("[DIAG][%s] evt=enc_fail idr=%d ms=%u", name.c_str(), idr ? 1 : 0, ms);
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

    // Ảnh chụp trạng thái cho UI: thread Recv ghi mỗi giây, main thread đọc.
    std::mutex statusMutex;
    std::vector<AgentSourceStatus> statusRows;

    // "Host thắng": một bộ theo dõi dùng chung cho mọi nguồn.
    LocalInputMonitor localInputMon;

    // Máy này trả lời các câu hỏi TRƯỚC KHI có phiên — "đang chia sẻ gì?"
    // (LIST_SOURCES), "còn sống? bao xa?" (PING sessionId=0). Beacon chỉ DỰNG byte
    // trả lời; gửi là việc của vòng Recv, về đúng nơi vừa hỏi.
    deskhub::Beacon beacon;

    uint32_t startBitrate = 0, minBitrate = 1'000'000;

    // Địa chỉ nguồn của gói đang xử lý (chỉ thread Recv dùng). Callback `send` của
    // HostSession gửi theo biến này.
    NetAddr replyAddr{};

    void RecvLoop();
    void StartPipeline(SourcePipeline* p, int portalFd);
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

std::string AgentLoop::LastError() {
    std::lock_guard<std::mutex> lk(errMutex_);
    return lastError_;
}

SourcePipeline* AgentLoop::Impl::MakePipeline(const AgentSource& s) {
    auto p = std::make_unique<SourcePipeline>(startBitrate, minBitrate);
    p->sourceId = nextSourceId++;
    p->nodeId = s.nodeId;
    p->name = s.name;
    p->srcX = s.x;
    p->srcY = s.y;
    pipes.push_back(std::move(p));
    return pipes.back().get();
}

// Nối chuỗi capture→encode→gửi cho MỘT nguồn rồi khởi động capture.
void AgentLoop::Impl::StartPipeline(SourcePipeline* p, int portalFd) {
    UdpSocket* sockPtr = &sock;

    // NAL vừa nén xong (thread capture, đồng bộ ngay sau Encode) -> cắt gói -> UDP.
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
        // Packetizer là single-thread. Thread Recv chỉ đặt ý muốn qua atomic, việc
        // bật/tắt thật diễn ra ở đây — khỏi cần khoá.
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
        // Sự kiện IDR: ghi lại cho thread Recv in — cỡ IDR là con số quyết định
        // chẩn đoán chùm mất gói (docs/06 §7b).
        if (pkts && keyframe) {
            p->dgIdrCount.fetch_add(1, std::memory_order_relaxed);
            p->dgIdrPkts.store(uint32_t(pkts), std::memory_order_relaxed);
            p->dgIdrBurstMs.store(burstMs, std::memory_order_relaxed);
            p->dgIdrBytes.store(uint64_t(size), std::memory_order_release);
        }
    };

    const uint32_t fps = opt.fps;
    // Tạo encoder nếu chưa có. GỌI DƯỚI encMutex. false = không dựng được.
    auto ensureEncoder = [p, fps, onPacket](uint32_t w, uint32_t h) -> bool {
        if (p->encoder && p->encoder->IsOpen()) return true;
        EncoderConfig cfg;
        cfg.width = w;
        cfg.height = h;
        // fps của BẬC HIỆN TẠI, không phải trần người dùng. Trên VA-API fps đi vào
        // cả VUI time_scale của SPS, nên encoder dựng lại phải mang đúng nhịp đang
        // chạy — lệch là client tính sai thời gian trình bày.
        cfg.fps = p->curFps.load(std::memory_order_relaxed);
        if (!cfg.fps) cfg.fps = fps;
        cfg.bitrateBps = p->curBitrateBps.load(std::memory_order_relaxed);
        cfg.onPacket = onPacket;
        auto enc = std::make_unique<VaEncoder>();
        if (!enc->Init(cfg)) {
            LOGE("[Agent][%s] VA-API refused to start an encoder.", p->name.c_str());
            p->failed.store(true);
            return false;
        }
        p->encoder = std::move(enc);
        return true;
    };
    p->ensureEncoderFn = ensureEncoder;

    // Đường nóng của nguồn này. CHẠY TRÊN THREAD PIPEWIRE — không phải thread Recv.
    // Ba việc, theo thứ tự:
    //   1. Phát hiện đổi kích thước → vứt encoder, báo cho thread Recv.
    //   2. Bỏ frame nếu nguồn nhỏ hơn mức encoder nhận (trạng thái TẠM).
    //   3. Encode.
    // Giữ encMutex suốt từ bước 1: thread Recv cũng chạm vào encoder khi nó phải
    // nén lại frame tĩnh lúc client xin IDR.
    const uint32_t maxDim = opt.maxDim;
    auto onFrame = [p, ensureEncoder, maxDim](const LinuxFrameInfo& fi) {
        p->captured.fetch_add(1, std::memory_order_relaxed);
        if (p->failed.load()) return;
        if (!fi.width || !fi.height) return;

        p->nativeW.store(fi.width, std::memory_order_relaxed);
        p->nativeH.store(fi.height, std::memory_order_relaxed);

        // Cỡ MUỐN nén. Recv chưa kịp tính (frame đầu, chưa có client) thì tự tính
        // lấy từ trần người dùng — khung ra đúng cỡ ngay từ frame ĐẦU.
        uint32_t encW = p->wantW.load(std::memory_order_relaxed);
        uint32_t encH = p->wantH.load(std::memory_order_relaxed);
        if (!encW || !encH) {
            const deskhub::StreamSize t =
                deskhub::FitStreamSize(fi.width, fi.height, maxDim, 0, 0);
            encW = t.width;
            encH = t.height;
        }
        if (encW > fi.width) encW = fi.width;   // không bao giờ phóng to
        if (encH > fi.height) encH = fi.height;
        // VA-API từ chối kích thước lẻ và lỗi đó rất khó lần.
        encW &= ~1u;
        encH &= ~1u;
        if (!encW || !encH) return;

        std::lock_guard<std::mutex> lk(p->encMutex);

        // Cỡ NÉN đổi. Hai nguyên nhân, cùng một cách xử lý: nguồn đổi độ phân giải
        // thật, hoặc thang chất lượng vừa đổi bậc (wantW/wantH). Encoder gắn chặt với
        // cỡ cũ -> vứt, dựng lại ngay ở frame này. Cờ sizeChanged để Recv báo
        // RECONFIG + IDR.
        if (p->srcW.load() != encW || p->srcH.load() != encH) {
            if (p->srcW.load())
                LOGI("[Agent][%s] Encode size %ux%u -> %ux%u (source %ux%u), rebuilding encoder.",
                    p->name.c_str(), p->srcW.load(), p->srcH.load(), encW, encH, fi.width,
                    fi.height);
            p->srcW.store(encW);
            p->srcH.store(encH);
            p->encoder.reset();
            p->sizeChanged.store(true, std::memory_order_release);
        }

        // Nguồn nhỏ hơn mức encoder nhận. TRẠNG THÁI TẠM, không phải lỗi: bỏ qua
        // frame và giữ nguyên phiên.
        //
        // Chặn ở ĐÚNG chỗ này mới thoát được: trên nó là đoạn ghi nhận kích thước —
        // vẫn phải chạy, vì đó là thứ duy nhất cho ta biết màn hình đã trở lại cỡ
        // bình thường. Thoát sớm hơn (như `failed` làm) là tự bịt mắt.
        if (encW < kMinEncodeW || encH < kMinEncodeH) {
            if (!p->paused.exchange(true, std::memory_order_acq_rel))
                LOGI("[Agent][%s] Source too small to encode (%ux%u) — paused.", p->name.c_str(),
                    encW, encH);
            return;
        }
        if (p->paused.exchange(false, std::memory_order_acq_rel))
            LOGI("[Agent][%s] Source back to %ux%u — resuming.", p->name.c_str(), encW, encH);

        // ⚠ CỔNG NHỊP — thứ thực sự thi hành fps của thang.
        //   Khác macOS (SCStream có minimumFrameInterval, tự giảm nhịp), PipeWire
        //   giao frame theo nhịp đã thoả thuận lúc Start và không đổi được rẻ giữa
        //   chừng. Không có cổng này thì hạ fps CHỈ đổi mẫu số của bộ điều khiển tốc
        //   độ: vẫn nén đúng bấy nhiêu frame, mỗi frame lại được cấp nhiều bit hơn
        //   → bitrate thật VƯỢT ngân sách, đúng ngược điều thang định làm.
        //   Đặt sau nhánh paused và trước phần nén: bỏ frame ở đây là bỏ trước khi
        //   tốn bất cứ thứ gì, còn phần ghi nhận kích thước bên trên vẫn phải chạy.
        if (const uint32_t gateFps = p->curFps.load(std::memory_order_relaxed)) {
            const uint64_t minGapUs = 1'000'000ull / gateFps;
            const uint64_t last = p->lastEncodeUs.load(std::memory_order_relaxed);
            // Trừ một nửa mili-giây dung sai: frame tới sớm hơn hạn đúng vài chục
            // micro-giây là chuyện thường của mọi nguồn, và bỏ nó đi sẽ tụt nhịp
            // thật xuống dưới mức mong muốn một cách hệ thống.
            if (last && fi.timestampUs > last && fi.timestampUs - last + 500 < minGapUs) return;
            p->lastEncodeUs.store(fi.timestampUs, std::memory_order_relaxed);
        }

        p->lastFrameUs.store(fi.timestampUs, std::memory_order_relaxed);

        // Chặn ở đây chứ không sớm hơn: mọi bước trên (ghi nhận kích thước) vẫn
        // phải chạy TRƯỚC khi có client, vì Start đang đợi đúng srcW để dựng offer.
        if (!p->netReady.load(std::memory_order_acquire)) return;
        if (!ensureEncoder(encW, encH)) return;
        // Encode liên tục kể cả khi chưa có client (đơn giản, tốc độ bit ổn định);
        // NAL bị bỏ ở onPacket nếu chưa STREAMING.
        const bool idr = p->forceIdr.exchange(false);
        VaEncoder* enc = p->encoder.get();
        p->DiagEncode([enc, &fi, idr] { return enc->Encode(fi, fi.timestampUs, idr); }, idr);
    };

    if (!p->capture.Start(portalFd, p->nodeId, opt.fps, onFrame)) {
        LOGE("[Agent][%s] Failed to start capture — skipping this source.", p->name.c_str());
        p->failed.store(true);
    }
}

// Dựng phiên + injector cho MỘT nguồn đã biết kích thước.
void AgentLoop::Impl::AttachSession(SourcePipeline* p) {
    p->offer.width = uint16_t(p->srcW.load());
    p->offer.height = uint16_t(p->srcH.load());
    p->offer.fps = uint8_t(opt.fps);
    p->offer.bitrateBps = startBitrate;
    LOGI("[Agent] Source %u \"%s\": %ux%u @%ufps, %u Mbps.", p->sourceId, p->name.c_str(),
        p->offer.width, p->offer.height, opt.fps, opt.bitrateMbps);

    // Chuột/bàn phím LUÔN được chia sẻ (chốt 2026-07-27) — chỉ còn một đường.
    p->injector.SetLocalMonitor(&localInputMon);
    // Kích thước truyền cho injector là kích thước THẬT của stream (PipeWire đã
    // thoả thuận), không phải con số portal báo: hai số lệch nhau khi màn hình
    // dùng scale phân số, và lệch bao nhiêu thì con trỏ trượt bấy nhiêu.
    // Cỡ NATIVE, không phải cỡ nén: từ khi có trần độ phân giải + thang chất lượng,
    // srcW/srcH là cỡ khung GỬI ĐI và nó co giãn giữa phiên. Hình học chuột thì
    // không đổi — nó gắn với màn hình thật.
    p->injector.SetEnabled(p->injector.Init(p->srcX, p->srcY, p->nativeW.load(),
        p->nativeH.load(), opt.desktopX, opt.desktopY, opt.desktopW, opt.desktopH));

    UdpSocket* sockPtr = &sock;
    Impl* self = this;

    deskhub::HostCallbacks cb;
    cb.send = [sockPtr, self](std::span<const uint8_t> d) {
        sockPtr->SendTo(self->replyAddr, d.data(), d.size());
    };
    // Nguồn ngẫu nhiên mã hoá cho sessionId. core/ không đụng được API hệ điều
    // hành nên phải nối từ đây. Thiếu callback này thì HostSession từ chối MỌI kết
    // nối (fail closed).
    cb.randomBytes = [](std::span<uint8_t> out) {
        return RandomBytes(out.data(), out.size());
    };
    // Tính lại cỡ MUỐN nén từ cỡ native + hai trần + bậc thang. MỘT đường duy nhất
    // cho cả onHello lẫn onFeedback: hai bản của cùng chính sách là hai bản sẽ lệch.
    // Chỉ thread Recv gọi.
    const AgentOptions& o = opt;
    auto retarget = [p, &o]() -> deskhub::StreamSize {
        const uint32_t nw = p->nativeW.load(std::memory_order_relaxed);
        const uint32_t nh = p->nativeH.load(std::memory_order_relaxed);
        if (!nw || !nh) return {0, 0};
        deskhub::StreamSize t = deskhub::FitStreamSize(nw, nh, o.maxDim, p->cliW, p->cliH);
        const uint32_t pct = p->step.scalePct ? p->step.scalePct : 100;
        if (pct < 100) {
            t.width = (t.width * pct / 100u) & ~1u;
            t.height = (t.height * pct / 100u) & ~1u;
        }
        if (!t.width || !t.height) return {0, 0};
        p->wantW.store(t.width, std::memory_order_relaxed);
        p->wantH.store(t.height, std::memory_order_relaxed);
        return t;
    };

    // Client mới vừa chào và kèm cỡ màn hình của nó. Co luồng cho vừa NGAY BÂY GIỜ,
    // trước khi HELLO_ACK đi ra — client nhờ vậy dựng bộ giải mã đúng một lần thay
    // vì dựng rồi phải dựng lại sau một RECONFIG.
    cb.onHello = [p, &o, retarget](const deskhub::Hello& h) {
        p->cliW = h.maxWidth;
        p->cliH = h.maxHeight;
        p->step = deskhub::QualityStep{};
        const deskhub::StreamSize t = retarget();
        if (!t.width || !t.height) return;
        p->ladder = std::make_unique<deskhub::QualityLadder>(uint16_t(t.width),
            uint16_t(t.height), uint8_t(o.fps));
        p->step = p->ladder->current();
        p->curFps.store(p->step.fps, std::memory_order_relaxed);
        p->offer.width = uint16_t(t.width);
        p->offer.height = uint16_t(t.height);
        p->offer.fps = uint8_t(p->step.fps);
        p->offer.bitrateBps = p->curBitrateBps.load(std::memory_order_relaxed);
        p->session->SetOffer(p->offer);
        LOGI("[Agent][%s] Quality ladder: ceiling %ux%u @%ufps, %d rung(s).", p->name.c_str(),
            t.width, t.height, p->step.fps, p->ladder->rungCount());
    };
    cb.onStart = [p] {
        p->forceIdr.store(true); // IDR mở màn (kèm SPS/PPS — xem VaEncoder.h)
        LOGI("[Agent][%s] Client START — beginning video push.", p->name.c_str());
    };
    cb.onKeyframeRequest = [p] { p->forceIdr.store(true); };
    // GĐ7: client xin gửi lại các mảnh còn thiếu của một frame. Tra kho rồi phát
    // lại thẳng cho peer hiện tại. Rẻ hơn nhiều so với ép cả một IDR.
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
    // SET_FOCUS(false): client rời nguồn này giữa lúc có thể đang giữ phím — nhả
    // hết, không để kẹt. Nhánh true không còn việc gì: nguồn là cả màn hình.
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
    // GĐ5 congestion control, RIÊNG từng nguồn. Policy nằm ở
    // deskhub::BitrateController (core, test được offline); ở đây chỉ phần dính
    // thiết bị.
    cb.onFeedback = [p, retarget](const deskhub::Feedback& fb) {
        // RTT chỉ đo được ở phía client — FEEDBACK là đường duy nhất nó tới host.
        p->uiRttMs.store(fb.rttMs, std::memory_order_relaxed);

        const deskhub::BitrateDecision d = p->rate.Update(fb, NowUs());

        if (d.fecToggled) {
            p->wantFec.store(d.fecEnabled, std::memory_order_relaxed);
            LOGI("[Agent][%s] FEC %s (loss %u%%).", p->name.c_str(), d.fecEnabled ? "on" : "off",
                fb.lossPct);
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
        // pixel hay khung hình (deskhub::QualityLadder). Chạy MỖI lần Feedback kể cả
        // khi bitrate không đổi: thang có dwell riêng và cần nhịp đều để đếm.
        if (!p->ladder) return;
        if (!p->ladder->Update(p->rate.bitrateBps(), NowUs())) return;
        const deskhub::QualityStep prev = p->step;
        p->step = p->ladder->current();
        p->curFps.store(p->step.fps, std::memory_order_relaxed);
        const deskhub::StreamSize t = retarget();
        // ⚠ VA-API KHÔNG có núm chỉnh fps khi đang chạy: fps đi vào VUI time_scale
        //   của SPS, nên đổi nó là đổi SPS, và đổi SPS là bắt buộc phải có IDR mới.
        //   Nên bậc chỉ-đổi-fps ở đây vẫn phải dựng lại encoder — khác bản macOS
        //   (VtEncoder::SetFps chỉnh nóng được) và khác NVENC. Vứt encoder ở đây;
        //   onFrame dựng lại với curFps mới ở frame kế tiếp.
        if (prev.fps != p->step.fps) {
            std::lock_guard<std::mutex> lk(p->encMutex);
            p->encoder.reset();
        }
        LOGI("[Agent][%s] Quality %u%%@%ufps -> %u%%@%ufps (%ux%u, budget %.1f Mbps)",
            p->name.c_str(), prev.scalePct, prev.fps, p->step.scalePct, p->step.fps, t.width,
            t.height, p->rate.bitrateBps() / 1e6);
        p->qualityChanged.store(true, std::memory_order_release);
    };

    p->session = std::make_unique<deskhub::HostSession>(cb, p->offer);
    p->netReady.store(true, std::memory_order_release);
}

// Tắt hẳn MỘT nguồn. Idempotent (shutdownDone). CHỈ gọi từ thread Recv (hoặc từ
// Stop() sau khi thread Recv đã join).
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
        r.zeroCopy = p->capture.usingDmaBuf();
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
//   1. Kiểm tra đầu vào + phiên portal, mở socket.
//   2. Dựng SourcePipeline cho từng nguồn.
//   3. Khởi động capture — từ đây các thread PipeWire bắt đầu chạy.
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

    auto fail = [this](std::string msg) {
        LOGE("[Agent] %s", msg.c_str());
        std::lock_guard<std::mutex> lk(errMutex_);
        lastError_ = std::move(msg);
        return false;
    };

    if (sources.empty()) return fail("No display to share.");
    if (sources.size() > deskhub::kMaxSources)
        return fail("At most " + std::to_string(deskhub::kMaxSources) +
                    " sources can be shared at once.");

    // Phiên portal phải CÒN MỞ: nó là thứ giữ quyền quay màn hình và giữ fd
    // PipeWire. Người dùng bấm "Stop sharing" trên chỉ báo của compositor giữa lúc
    // này thì ta không còn gì để bắt.
    PortalScreenCast& portal = PortalScreenCast::Instance();
    if (!portal.isOpen())
        return fail("The screen-capture permission is gone — press Share again.");

    // MỘT cổng duy nhất, không có phương án hai (giống Windows/macOS). Client chỉ
    // biết gõ IP — một host nhảy sang cổng khác là một host không ai kết nối tới được.
    if (!im->sock.Open(kDeskhubPort))
        return fail("UDP port " + std::to_string(kDeskhubPort) +
                    " is not available — another Deskhub is probably still running.");
    im->sock.SetRecvTimeout(100);

    im->startBitrate = opt.bitrateMbps * 1'000'000u;
    // Sàn bitrate: dưới mức này hình nát tới mức vô dụng, thà bỏ frame còn hơn.
    im->minBitrate = 1'000'000u;

    LOGI("[Agent] Listening on UDP port %u. On the other machine, enter one of:",
        unsigned(kDeskhubPort));
    for (const auto& a : ListLocalIPv4()) LOGI("    %s    (%s)", a.ip.c_str(), a.name.c_str());

    // --- Dựng + khởi động capture cho từng nguồn ---
    for (const AgentSource& s : sources) im->StartPipeline(im->MakePipeline(s), portal.pipewireFd());

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
        im->sock.Close();
        return fail("No usable source — the compositor sent no frame.");
    }

    for (SourcePipeline* p : im->live) im->AttachSession(p);

    // "Host thắng" khi hai bên cùng điều khiển. Luôn cần: input luôn được chia sẻ.
    im->localInputMon.Start();
    LOGI("[Agent] Sharing %zu source(s). Waiting for client...", im->live.size());

    im->PublishStatus();
    {
        std::lock_guard<std::mutex> lk(errMutex_);
        lastError_.clear();
    }
    running_.store(true, std::memory_order_release);
    im->recvThread = std::thread([this, im] {
        im->RecvLoop();
        // Vòng Recv tự thoát (lỗi socket / hết nguồn sống) — hạ cờ để UI poll thấy
        // phiên đã kết thúc. An toàn: Stop() join thread này trước khi impl_ và
        // AgentLoop bị huỷ.
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

    // Quét TOÀN BỘ pipes chứ không riêng live: nguồn đang chờ frame đầu và nguồn
    // đã tắt giữa phiên đều nằm ngoài live; ShutdownPipeline idempotent nên nguồn
    // nào dọn rồi chỉ là no-op, còn tổng kết thì tính cả nguồn đã tắt giữa chừng.
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
void AgentLoop::Impl::RecvLoop() {
    uint8_t buf[deskhub::kMaxDatagram];
    uint8_t beaconBuf[deskhub::kMaxDatagram]; // riêng, không dùng chung với buf nhận:
                                              // Reply() đọc gói đến trong lúc ghi ra
    uint64_t lastStatUs = NowUs();
    // Thời gian BẬN dài nhất của một vòng Recv trong cửa sổ 1s (không tính lúc chờ
    // recvfrom). Vòng này mà nghẽn thì buffer UDP của kernel gánh — tràn là mất
    // gói thật. Chỉ thread Recv chạm nên không cần atomic.
    uint32_t dgLoopBusyMaxMs = 0;

    while (!quit.load()) {
        // Hết nguồn sống là hết phiên (giống Windows/macOS): một phiên không còn
        // màn hình nào chỉ là một cửa sổ trống.
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
            // chiếm chỗ client thật ở đó.
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
                        LOGI("[Agent][%s] Peer: %s", dst->name.c_str(), from.ToString().c_str());
                    }
                }
            }
        }

        for (SourcePipeline* p : live) {
            if (p->failed.load()) continue;
            // Nguồn biến mất giữa phiên (người dùng bấm "Stop sharing" trên chỉ
            // báo của compositor) — dọn ngay thay vì để người xem nhìn khung đứng hình.
            if (p->capture.Closed() && !p->shutdownDone) {
                LOGI("[Agent][%s] Source closed — stopping this source.", p->name.c_str());
                ShutdownPipeline(p);
                PublishStatus();
                continue;
            }
            p->session->Tick(now);

            // Sự kiện IDR do thread capture ghi lại — in ở đây để I/O không nằm
            // trên đường nóng.
            if (const uint64_t ib = p->dgIdrBytes.exchange(0, std::memory_order_acquire)) {
                LOGI("[DIAG][%s] evt=idr bytes=%" PRIu64 " pkts=%u burst_ms=%u", p->name.c_str(),
                    ib, p->dgIdrPkts.load(std::memory_order_relaxed),
                    p->dgIdrBurstMs.load(std::memory_order_relaxed));
            }

            // Nguồn vừa đổi kích thước (thread capture đã vứt encoder). Báo client
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
                    // Trên VA-API, đổi fps cũng đổi SPS (VUI time_scale) nên bậc
                    // chỉ-đổi-fps cũng cần IDR — khác macOS/NVENC. Encoder đã bị vứt
                    // ở onFeedback, IDR ở đây là mảnh còn lại của cùng việc đó.
                    p->forceIdr.store(true);
                }
            }

            // Nguồn đang TĨNH (compositor chỉ phát frame khi nội dung đổi) —
            // encoder không có input mới. Hai nhu cầu gộp chung một chỗ:
            //   1. Yêu cầu IDR đang treo (>200ms không có frame) → nén lại frame
            //      nguồn cuối với IDR, không thì client vào xem màn hình tĩnh sẽ
            //      đen mãi.
            //   2. KEEPALIVE ~2fps: giữ đồng hồ trình bày của client chạy. Nội dung
            //      không đổi nên P-frame chỉ vài KB.
            const uint64_t sinceFrameUs = now - p->lastFrameUs.load(std::memory_order_relaxed);
            const bool wantIdrFlush = p->forceIdr.load() && sinceFrameUs > 200'000;
            const bool wantKeepalive = sinceFrameUs > 500'000 &&
                                       now - p->lastKeepaliveUs >= 500'000;
            if (p->session->state() == deskhub::HostSession::State::Streaming &&
                (wantIdrFlush || wantKeepalive)) {
                std::lock_guard<std::mutex> lk(p->encMutex);
                if (p->encoder && p->encoder->haveSourceFrame()) {
                    // Timestamp MỚI chứ không dùng lại mốc của frame cũ: client
                    // tính e2e từ trường này, và bơm lại một mốc cũ làm nó báo trễ
                    // hàng chục giây (đúng lỗi bản Windows gặp ngày 21/07/2026).
                    const bool idr = p->forceIdr.exchange(false);
                    VaEncoder* enc = p->encoder.get();
                    p->DiagEncode([enc, now, idr] { return enc->EncodeLast(now, idr); }, idr);
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
                // injector), KHÔNG phải bằng chứng phím đã tới ứng dụng. Injector
                // còn vứt tiếp khi "host thắng" — `skipped` là con số duy nhất lộ
                // ra chuyện đó. Thiếu nó thì "gõ không ăn" không phân biệt được với
                // "không nhận được gói".
                LOGI(
                    "[Agent t=%s][%s] %-9s | capture %.0f fps | send %.0f fps, %.0f kbps"
                    " | input %" PRIu64 " (lost %" PRIu64 ", skipped %" PRIu64 ")",
                    deskhubp::LocalTimeHms().c_str(), p->name.c_str(), StateName(p->session->state()), p->statCaptureFps,
                    p->statSendFps, p->statSendKbps, ist.applied, ist.lost,
                    p->injector.skipped());
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
                    " enc_lat_ms=%.1f/%u idr=%u"
                    " burst_ms_max=%u send_fail=%u zerocopy=%d",
                    p->name.c_str(), ec ? double(es) / ec : 0.0, em,
                    lc ? double(ls) / lc : 0.0,
                    p->dgEncLatMax.exchange(0, std::memory_order_relaxed),
                    p->dgIdrCount.exchange(0, std::memory_order_relaxed),
                    p->dgBurstMsMax.exchange(0, std::memory_order_relaxed),
                    p->dgSendFail.exchange(0, std::memory_order_relaxed),
                    p->capture.usingDmaBuf() ? 1 : 0);
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
