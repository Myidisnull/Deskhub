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
// LIÊN QUAN: agent/AgentLoop.h (AgentSource/AgentOptions/AgentSourceStatus),
//            client/ClientLoop.cpp (phía đối diện), deskhub/session/HostSession.h,
//            deskhub/transport/Packetizer.h, client/windows/AgentLoop.cpp,
//            docs/06-phase3-transport.md §4
// =============================================================================
#include "agent/AgentLoop.h"

#include <CoreVideo/CVPixelBuffer.h> // CVPixelBufferRetain/Release — API C thuần

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <functional>
#include <utility>

#include "Log.h"
#include "agent/ClipboardSync.h"
#include "agent/InputInjector.h"
#include "agent/LocalInputMonitor.h"
#include "agent/Permissions.h"
#include "agent/ScreenCapture.h"
#include "agent/VtEncoder.h"
#include "deskhubp/Clock.h"
#include "net/NetInfo.h"
#include "net/UdpSocket.h"

#include "deskhub/control/BitrateController.h"
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
    CaptureTarget target;
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
    // khỏi `failed` vì gộp chung thì cửa sổ thu nhỏ sẽ giết phiên vĩnh viễn: onFrame
    // thoát ngay ở đầu hàm nên không bao giờ thấy cửa sổ mở lại (bài học 21/07/2026
    // của bản Windows).
    std::atomic<bool> paused{false};
    std::atomic<bool> forceIdr{false};
    std::atomic<uint64_t> peerPacked{0}; // NetAddr::Pack của client hiện tại (0 = chưa có)
    std::atomic<uint64_t> bytesSent{0}, framesSent{0};
    std::atomic<uint32_t> captured{0};
    std::atomic<uint32_t> nextFrameId{0};

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
    // Nguồn thêm giữa phiên: capture đã chạy, đang đợi frame đầu để biết kích thước
    // rồi mới AttachSession được. pair = (pipeline, hạn chót µs).
    std::vector<std::pair<SourcePipeline*, uint64_t>> pendingAdds;

    // Cấp sourceId tăng dần và KHÔNG tái dùng id của nguồn đã tắt: client còn cầm
    // SOURCE_LIST cũ mà HELLO lại trúng một nguồn mới toanh thì xem nhầm màn hình.
    uint8_t nextSourceId = 0;

    // Hộp thư lệnh từ UI -> thread Recv (đối ứng SessionWindow::TakeAdds/TakeRemoves).
    std::mutex cmdMutex;
    std::vector<AgentSource> pendingAddCmds;
    std::vector<uint8_t> pendingRemoveCmds;

    // Ảnh chụp trạng thái cho UI: thread Recv ghi mỗi giây, main thread đọc.
    std::mutex statusMutex;
    std::string statusLine;
    std::vector<AgentSourceStatus> statusRows;
    std::vector<std::string> addresses;

    // Clipboard máy host (GĐ8).
    ClipboardSync clipSync;
    std::mutex clipMutex;
    std::string clipPendingText;
    bool clipPending = false;

    // "Host thắng": một bộ theo dõi dùng chung cho mọi nguồn.
    LocalInputMonitor localInputMon;

    uint32_t startBitrate = 0, minBitrate = 1'000'000;

    // Địa chỉ nguồn của gói đang xử lý (chỉ thread Recv dùng). Callback `send` của
    // HostSession gửi theo biến này.
    NetAddr replyAddr{};

    void RecvLoop();
    void StartPipeline(SourcePipeline* p);
    void AttachSession(SourcePipeline* p);
    void ShutdownPipeline(SourcePipeline* p);
    SourcePipeline* MakePipeline(const AgentSource& s);
    void PublishStatus(uint64_t now, double secs);
};

AgentLoop::AgentLoop() = default;

AgentLoop::~AgentLoop() {
    Stop();
}

std::string AgentLoop::StatusLine() {
    if (!impl_) return {};
    std::lock_guard<std::mutex> lk(impl_->statusMutex);
    return impl_->statusLine;
}

std::vector<AgentSourceStatus> AgentLoop::Status() {
    if (!impl_) return {};
    std::lock_guard<std::mutex> lk(impl_->statusMutex);
    return impl_->statusRows;
}

std::vector<std::string> AgentLoop::LocalAddresses() {
    if (!impl_) return {};
    std::lock_guard<std::mutex> lk(impl_->statusMutex);
    return impl_->addresses;
}

void AgentLoop::AddSource(const AgentSource& s) {
    if (!impl_) return;
    std::lock_guard<std::mutex> lk(impl_->cmdMutex);
    impl_->pendingAddCmds.push_back(s);
}

void AgentLoop::RemoveSource(uint8_t sourceId) {
    if (!impl_) return;
    std::lock_guard<std::mutex> lk(impl_->cmdMutex);
    impl_->pendingRemoveCmds.push_back(sourceId);
}

SourcePipeline* AgentLoop::Impl::MakePipeline(const AgentSource& s) {
    auto p = std::make_unique<SourcePipeline>(startBitrate, minBitrate);
    p->sourceId = nextSourceId++;
    p->target = s.target;
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
    // Tạo encoder nếu chưa có. GỌI DƯỚI encMutex. false = không dựng được session.
    auto ensureEncoder = [p, fps, onPacket](uint32_t w, uint32_t h) -> bool {
        if (p->encoder && p->encoder->IsOpen()) return true;
        EncoderConfig cfg;
        cfg.width = w;
        cfg.height = h;
        cfg.fps = fps;
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

    if (!p->capture.Start(p->target, opt.fps, onFrame)) {
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

    if (opt.allowInput) {
        p->injector.SetLocalMonitor(&localInputMon);
        p->injector.SetEnabled(p->injector.Init(p->target));
    } else {
        p->injector.SetEnabled(false);
    }

    UdpSocket* sockPtr = &sock;
    Impl* self = this;

    deskhub::HostCallbacks cb;
    cb.send = [sockPtr, self](std::span<const uint8_t> d) {
        sockPtr->SendTo(self->replyAddr, d.data(), d.size());
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
    // GĐ8: client vừa copy văn bản -> đặt vào clipboard máy host. ClipboardSync tự
    // khử trùng nội dung nên nhiều phiên cùng gửi một bản copy cũng vô hại.
    cb.onClipboard = [self](std::string text) { self->clipSync.SetRemoteText(text); };
    // Client chuyển sang xem nguồn này -> kéo đúng ứng dụng đó lên trước. Chỉ MỘT
    // ứng dụng được foreground, mà CGEventPost bơm vào ứng dụng foreground; không có
    // bước này thì client xem N nguồn nhưng chỉ điều khiển được nguồn nào người ở
    // máy host tự bấm vào.
    // Gác bằng enabled(): không cho điều khiển thì cũng không cho giành foreground.
    cb.onFocus = [p](bool focused) {
        if (!p->injector.enabled()) return;
        if (!focused) {
            p->injector.ReleaseAll(); // client rời đi giữa lúc giữ phím
            return;
        }
        if (!p->injector.FocusTarget())
            LOGW(
                "[Agent][%s] Could not bring this window to the front — "
                "click it once on this Mac.",
                p->name.c_str());
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
        const deskhub::BitrateDecision d = p->rate.Update(fb, NowUs());

        if (d.fecToggled) {
            p->wantFec.store(d.fecEnabled, std::memory_order_relaxed);
            LOGI("[Agent][%s] FEC %s (loss %u%%).", p->name.c_str(),
                d.fecEnabled ? "on" : "off", fb.lossPct);
        }

        if (!d.changeBitrate) return;

        const uint32_t cur = p->rate.bitrateBps();
        std::lock_guard<std::mutex> lk(p->encMutex);
        // Encoder từ chối thì KHÔNG commit: lần Feedback sau tính lại từ mức cũ.
        if (p->encoder && p->encoder->SetBitrate(d.bitrateBps)) {
            p->rate.CommitBitrate(d.bitrateBps);
            p->curBitrateBps.store(d.bitrateBps, std::memory_order_relaxed);
            LOGI("[Agent][%s] Bitrate %.1f -> %.1f Mbps (loss %u%%, RTT %u ms)",
                p->name.c_str(), cur / 1e6, d.bitrateBps / 1e6, fb.lossPct, fb.rttMs);
        }
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
void AgentLoop::Impl::PublishStatus(uint64_t now, double secs) {
    std::vector<AgentSourceStatus> rows;
    for (SourcePipeline* p : live) {
        if (p->failed.load() || p->capture.Closed()) continue;
        AgentSourceStatus r;
        r.sourceId = p->sourceId;
        r.name = p->name;
        r.width = p->srcW.load();
        r.height = p->srcH.load();
        r.viewerConnected = p->peerPacked.load(std::memory_order_relaxed) != 0;
        r.captureFps = p->statCaptureFps;
        r.sendFps = p->statSendFps;
        r.sendKbps = p->statSendKbps;
        rows.push_back(std::move(r));
    }
    for (const auto& pr : pendingAdds) {
        AgentSourceStatus r;
        r.sourceId = pr.first->sourceId;
        r.name = pr.first->name;
        r.starting = true;
        rows.push_back(std::move(r));
    }

    char line[192];
    std::snprintf(line, sizeof(line), "Sharing %zu source(s) on UDP port %u",
        rows.size(), unsigned(sock.IsOpen() ? opt.port : 0));

    (void)now;
    (void)secs;
    std::lock_guard<std::mutex> lk(statusMutex);
    statusRows = std::move(rows);
    statusLine = line;
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
        LOGE("[Agent] No source selected.");
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

    // Ưu tiên cổng người dùng chọn, kẹt thì +1 dần tới cổng trống kế tiếp — một host
    // cũ còn chạy ngầm không còn chặn được phiên mới. Cổng THẬT mới là cái phải in
    // ra cho máy kia gõ, không phải opt.port ban đầu.
    constexpr int kPortTries = 64;
    uint16_t boundPort = opt.port;
    bool opened = false;
    for (int i = 0; i < kPortTries; ++i) {
        const int p = int(opt.port) + i;
        if (p <= 0 || p > 65535) break;
        boundPort = uint16_t(p);
        if (im->sock.Open(boundPort)) {
            opened = true;
            break;
        }
    }
    if (!opened) {
        LOGE("[Agent] No free UDP port from %u to %u.", unsigned(opt.port),
            unsigned(opt.port) + kPortTries - 1);
        return false;
    }
    if (boundPort != opt.port)
        LOGI(
            "[Agent] Port %u was busy — using %u instead. Tell the other person to "
            "use this port.",
            unsigned(opt.port), unsigned(boundPort));
    im->opt.port = boundPort;
    port_.store(boundPort, std::memory_order_relaxed);
    im->sock.SetRecvTimeout(100);

    im->startBitrate = opt.bitrateMbps * 1'000'000u;
    // Sàn bitrate: dưới mức này hình nát tới mức vô dụng, thà bỏ frame còn hơn. Đây
    // là tham số `minBps` của BitrateController — nó không bao giờ tụt quá đây.
    im->minBitrate = 1'000'000u;

    {
        std::lock_guard<std::mutex> lk(im->statusMutex);
        for (const auto& a : ListLocalIPv4())
            im->addresses.push_back(a.ip + "  (" + a.name + ")");
    }
    LOGI("[Agent] Listening on UDP port %u. On the other machine, enter one of:",
        boundPort);
    for (const auto& a : im->addresses) LOGI("    %s:%u", a.c_str(), boundPort);

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

    // Đồng bộ clipboard (GĐ8): copy ở máy host -> hộp thư -> vòng Recv gửi qua phiên
    // đang streaming đầu tiên.
    im->clipSync.Start([im](const std::string& utf8) {
        std::lock_guard<std::mutex> lk(im->clipMutex);
        im->clipPendingText = utf8;
        im->clipPending = true;
    });

    for (SourcePipeline* p : im->live) im->AttachSession(p);

    // "Host thắng" khi hai bên cùng điều khiển. Chỉ cần khi cho phép điều khiển.
    if (opt.allowInput) {
        im->localInputMon.Start();
        const bool ax = macperm::HasAccessibility();
        LOGI("[Agent] Client control allowed (mouse + keyboard). Accessibility: %s%s",
            ax ? "YES" : "NO",
            ax ? "" : " — input will be silently dropped until it is granted");
    } else {
        LOGI("[Agent] VIEW ONLY — input from client is ignored.");
    }
    LOGI("[Agent] Sharing %zu source(s). Waiting for client...", im->live.size());

    im->PublishStatus(NowUs(), 1.0);
    running_.store(true, std::memory_order_release);
    im->recvThread = std::thread([im] { im->RecvLoop(); });
    return true;
}

void AgentLoop::Stop() {
    if (!impl_) return;
    Impl* im = impl_.get();
    im->quit.store(true);
    if (im->recvThread.joinable()) im->recvThread.join();
    running_.store(false, std::memory_order_release);

    im->clipSync.Stop();
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
    port_.store(0, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Vòng Recv (thread riêng), dùng chung cho mọi nguồn
// ---------------------------------------------------------------------------
// Mỗi vòng làm bốn việc, theo thứ tự:
//   1. Thi hành lệnh từ UI (Add / Stop selected) + nuôi danh sách nguồn đang chờ.
//   2. recvfrom — chặn tối đa 100 ms, nên vòng lặp luôn quay đủ nhanh để Tick.
//   3. Định tuyến gói vừa nhận về đúng SourcePipeline (xem sơ đồ đầu file).
//   4. Tick mọi phiên + thống kê mỗi giây.
//
// Một cửa sổ đóng KHÔNG giết cả phiên: người dùng còn nút Add để thêm nguồn mới.
void AgentLoop::Impl::RecvLoop() {
    uint8_t buf[deskhub::kMaxDatagram];
    uint64_t lastStatUs = NowUs();
    // Thời gian BẬN dài nhất của một vòng Recv trong cửa sổ 1s (không tính lúc chờ
    // recvfrom). Vòng này mà nghẽn thì buffer UDP của kernel gánh — tràn là mất gói
    // thật. Chỉ thread Recv chạm nên không cần atomic.
    uint32_t dgLoopBusyMaxMs = 0;

    while (!quit.load()) {
        // --- Lệnh từ UI ---
        bool rosterChanged = false;
        std::vector<AgentSource> adds;
        std::vector<uint8_t> removes;
        {
            std::lock_guard<std::mutex> lk(cmdMutex);
            adds.swap(pendingAddCmds);
            removes.swap(pendingRemoveCmds);
        }
        for (AgentSource& s : adds) {
            // Trần kMaxSources tính trên nguồn CÒN SỐNG + đang chờ, không phải tổng
            // đã từng share — tắt bớt rồi thêm lại thoải mái.
            size_t aliveCnt = pendingAdds.size();
            for (SourcePipeline* q : live)
                if (!q->failed.load() && !q->capture.Closed()) ++aliveCnt;
            if (aliveCnt >= deskhub::kMaxSources) {
                LOGW("[Agent] Cannot add \"%s\": already sharing %zu sources.",
                    s.name.c_str(), aliveCnt);
                continue;
            }
            SourcePipeline* p = MakePipeline(s);
            StartPipeline(p);
            LOGI("[Agent][%s] Added — waiting for first frame.", p->name.c_str());
            // Start hỏng thì failed đã bật — vòng pending dưới sẽ không bao giờ thấy
            // nó, nên đừng cho vào danh sách chờ.
            if (!p->failed.load()) pendingAdds.push_back({p, NowUs() + 10'000'000ull});
            rosterChanged = true;
        }
        for (uint8_t id : removes) {
            for (auto& up : pipes) {
                if (up->sourceId != id || up->shutdownDone) continue;
                LOGI("[Agent][%s] Stopped by the user.", up->name.c_str());
                ShutdownPipeline(up.get());
                rosterChanged = true;
            }
        }

        // --- Nguồn đang chờ: frame đầu về thì vào phiên, quá hạn/hỏng thì bỏ ---
        for (auto it = pendingAdds.begin(); it != pendingAdds.end();) {
            SourcePipeline* p = it->first;
            if (p->failed.load()) { // StartPipeline hỏng muộn, hoặc vừa bị Stop
                it = pendingAdds.erase(it);
                rosterChanged = true;
                continue;
            }
            if (p->srcW.load()) {
                AttachSession(p);
                live.push_back(p);
                it = pendingAdds.erase(it);
                rosterChanged = true;
                continue;
            }
            if (NowUs() > it->second) {
                LOGW("[Agent][%s] No frame within 10s — not sharing this source.",
                    p->name.c_str());
                ShutdownPipeline(p);
                it = pendingAdds.erase(it);
                rosterChanged = true;
                continue;
            }
            ++it;
        }
        if (rosterChanged) PublishStatus(NowUs(), 1.0);

        NetAddr from;
        const int n = sock.RecvFrom(buf, sizeof(buf), from);
        const uint64_t now = NowUs();
        if (n < 0) {
            LOGE("[Agent] Socket error — stopping.");
            break;
        }

        if (n > 0) {
            replyAddr = from;
            const auto span = std::span<const uint8_t>(buf, size_t(n));
            const auto h = deskhub::ParseCommonHeader(span);
            if (h && h->type == deskhub::MsgType::ListSources) {
                // Chỉ liệt kê nguồn còn sống, kèm kích thước hiện tại.
                std::vector<deskhub::SourceInfo> infos;
                for (SourcePipeline* p : live) {
                    if (p->failed.load() || p->capture.Closed()) continue;
                    deskhub::SourceInfo si;
                    si.sourceId = p->sourceId;
                    si.width = uint16_t(p->srcW.load());
                    si.height = uint16_t(p->srcH.load());
                    // Cả màn hình hay một cửa sổ — client vẽ biểu tượng khác nhau,
                    // và hệ quả riêng tư của hai thứ này khác hẳn nhau.
                    si.kind = p->target.isDisplay ? deskhub::SourceKind::Display
                                                  : deskhub::SourceKind::Window;
                    si.name = p->name;
                    infos.push_back(std::move(si));
                }
                const size_t sn = deskhub::BuildSourceList(buf, infos);
                if (sn) sock.SendTo(from, buf, sn);
            } else if (h) {
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

        // Clipboard máy host vừa đổi -> gửi cho client (GĐ8). Gửi qua phiên ĐANG
        // streaming đầu tiên là đủ: các phiên đều là cùng một client, và bên nhận
        // khử trùng theo nội dung. replyAddr phải đặt về peer của phiên đó vì
        // callback send gửi theo replyAddr (bình thường do gói đến đặt).
        {
            std::string t;
            {
                std::lock_guard<std::mutex> lk(clipMutex);
                if (clipPending) {
                    t = std::move(clipPendingText);
                    clipPending = false;
                }
            }
            if (!t.empty()) {
                for (SourcePipeline* p : live) {
                    if (p->failed.load() || !p->session ||
                        p->session->state() != deskhub::HostSession::State::Streaming)
                        continue;
                    const uint64_t pp = p->peerPacked.load(std::memory_order_acquire);
                    if (!pp) continue;
                    replyAddr = NetAddr::Unpack(pp);
                    p->session->SendClipboard(t);
                    break;
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
                PublishStatus(now, 1.0);
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
            if (!p->paused.load(std::memory_order_acquire) &&
                p->sizeChanged.exchange(false, std::memory_order_acq_rel)) {
                p->offer.width = uint16_t(p->srcW.load());
                p->offer.height = uint16_t(p->srcH.load());
                p->offer.bitrateBps = p->curBitrateBps.load(std::memory_order_relaxed);
                p->session->SetOffer(p->offer); // HELLO phát lại sau phải mang số mới
                const uint64_t pp = p->peerPacked.load(std::memory_order_acquire);
                if (pp && p->session->state() == deskhub::HostSession::State::Streaming) {
                    deskhub::Reconfig rc{p->offer.width, p->offer.height, p->offer.bitrateBps};
                    uint8_t rbuf[deskhub::kMaxDatagram];
                    const size_t rn = deskhub::BuildReconfig(rbuf, p->session->sessionId(), rc);
                    if (rn) sock.SendTo(NetAddr::Unpack(pp), rbuf, rn);
                    p->forceIdr.store(true);
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
                LOGI(
                    "[Agent][%s] %-9s | capture %.0f fps | send %.0f fps, %.0f kbps"
                    " | input %" PRIu64 " (lost %" PRIu64 ", skipped %" PRIu64 ")",
                    p->name.c_str(), StateName(p->session->state()),
                    p->statCaptureFps, p->statSendFps, p->statSendKbps,
                    ist.applied, ist.lost, p->injector.skipped());
                p->lastCaptured = cap;
                p->lastBytes = by;
                p->lastFrames = fr;

                const uint32_t ec = p->dgEncCount.exchange(0, std::memory_order_relaxed);
                const uint32_t es = p->dgEncMsSum.exchange(0, std::memory_order_relaxed);
                const uint32_t em = p->dgEncMsMax.exchange(0, std::memory_order_relaxed);
                LOGI(
                    "[DIAG][%s] evt=sum enc_ms_avg=%.1f enc_ms_max=%u idr=%u"
                    " burst_ms_max=%u send_fail=%u",
                    p->name.c_str(), ec ? double(es) / ec : 0.0, em,
                    p->dgIdrCount.exchange(0, std::memory_order_relaxed),
                    p->dgBurstMsMax.exchange(0, std::memory_order_relaxed),
                    p->dgSendFail.exchange(0, std::memory_order_relaxed));
            }
            LOGI("[DIAG][agent] evt=sum loop_busy_ms_max=%u", dgLoopBusyMaxMs);
            dgLoopBusyMaxMs = 0;
            PublishStatus(now, secs);
            lastStatUs = now;
        }

        // Vòng này bận bao lâu (từ lúc recvfrom trả về tới đây). Nghẽn nặng thì báo
        // ngay, không đợi cửa sổ 1s.
        const uint32_t busyMs = uint32_t((NowUs() - now) / 1000);
        if (busyMs > dgLoopBusyMaxMs) dgLoopBusyMaxMs = busyMs;
        if (busyMs > 250) LOGW("[DIAG][agent] evt=recv_stall busy_ms=%u", busyMs);
    }
}
