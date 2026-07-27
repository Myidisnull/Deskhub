// =============================================================================
// AgentLoop.cpp — vai trò HOST. Nơi mọi thứ của phía chia sẻ được ghép lại.
//
// NHIỆM VỤ
//   Ghép chuỗi bắt hình/mã hoá (GĐ2) với tầng mạng (GĐ3), rồi nhân lên cho nhiều
//   nguồn (GĐ6). Đây là file điều phối lớn nhất phía host — bản thân nó không cài
//   đặt thuật toán nào, mà nối các mảnh đã có và quản lý luồng giữa chúng.
//
// ⚠ KIẾN TRÚC LUỒNG — điều quan trọng nhất phải nắm trước khi sửa
//
//   MỖI NGUỒN có một thread FrameArrived riêng (do WGC tạo):
//       capture → encoder → onPacket(NAL) → Packetizer → Pacer → sock.SendTo(peer)
//
//   MỘT thread Recv DÙNG CHUNG cho mọi nguồn (chính là thread gọi RunAgent):
//       recvfrom (timeout 100ms) → định tuyến gói → Tick mọi phiên → thống kê 1s/lần
//
//   Nghĩa là với N nguồn thì có N+1 thread, và mọi trạng thái đi qua ranh giới giữa
//   chúng phải là atomic hoặc được mutex bảo vệ. SourcePipeline bên dưới ghi rõ
//   từng trường thuộc về thread nào — ĐỌC PHẦN ĐÓ trước khi thêm trường mới.
//
// ĐỊNH TUYẾN GÓI ĐẾN — ba loại, ba cách tìm chủ
//   LIST_SOURCES → không thuộc phiên nào; trả SOURCE_LIST liệt kê mọi nguồn.
//   HELLO        → tìm theo hello.sourceId (lúc này chưa có sessionId).
//   Còn lại      → tìm theo sessionId, khớp với phiên của từng nguồn.
//
// VÌ SAO MỘT SOCKET CHUNG, KHÔNG PHẢI MỖI NGUỒN MỘT CỔNG
//   Người dùng chỉ phải mở một cổng firewall và chỉ phải nhớ một địa chỉ; client
//   hỏi LIST_SOURCES là thấy hết. Cái giá là phải tự định tuyến gói như trên.
//
// HAI CƠ CHẾ ĐÁNG CHÚ Ý
//   1. forceIdr là ATOMIC FLAG. Đặt từ thread Recv (onStart / onKeyframeRequest),
//      tiêu thụ ở lần Encode kế tiếp trên thread FrameArrived. Không gọi thẳng
//      encoder từ thread Recv được — nó thuộc thread kia (docs/06 §4).
//   2. CACHE FRAME CUỐI. WGC chỉ phát frame khi nội dung ĐỔI. Nguồn đang tĩnh
//      (menu, màn hình đứng im) mà client xin IDR thì không có frame nào để nén —
//      không cache thì client vào xem màn hình tĩnh sẽ đen VĨNH VIỄN.
//
// LIÊN QUAN: AgentLoop.h (AgentSource/AgentOptions), ClientLoop.cpp (phía đối
//            diện), deskhub/session/HostSession.h, deskhub/transport/Packetizer.h,
//            net/Pacer.h, docs/06-transport.md §4
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include "AgentLoop.h"

#include <atomic>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <wrl/client.h>

#include "capture/GpuSelect.h"
#include "ElevatedShare.h" // IsProcessElevated — chẩn đoán UIPI khi bật điều khiển
#include "net/Firewall.h"  // tự mở firewall để client không bị timeout
#include "input/InputInjector.h"
#include "input/LocalInputMonitor.h" // "host thắng" khi hai bên cùng điều khiển
#include "encode/IVideoEncoder.h"
#include "net/NetInfo.h"
#include "deskhubp/Clock.h"
#include "deskhubp/Random.h"
#include "net/UdpSocket.h"
#include "capture/ScreenCapture.h"
#include "AgentControl.h"
#include "Diag.h"

#include "deskhub/control/BitrateController.h"
#include "deskhub/session/Beacon.h" // trả lời LIST_SOURCES / PING dò trước phiên
#include "deskhub/session/HostSession.h"
#include "deskhub/transport/Packetizer.h"
#include "deskhub/transport/RetransmitCache.h"

namespace {

std::atomic<bool> g_ctrlC{false};

BOOL WINAPI CtrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        g_ctrlC.store(true);
        return TRUE;
    }
    return FALSE;
}

// Tên nguồn giữ dạng UTF-8 (để đi trên dây); cửa sổ phiên cần UTF-16 hiển thị.
std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
    return w;
}

const char* StateName(deskhub::HostSession::State s) {
    switch (s) {
        case deskhub::HostSession::State::Idle: return "IDLE";
        case deskhub::HostSession::State::Ready: return "READY";
        case deskhub::HostSession::State::Streaming: return "STREAMING";
    }
    return "?";
}

// Cỡ khung nhỏ nhất còn encode được. Encoder phần cứng từ chối khung quá nhỏ —
// NVENC trả "status=8 Frame Dimension less than the minimum supported value", MF
// trả 0xC00D6D76 ở SetOutputType. (Thời còn share cửa sổ, ca gặp thật là cửa sổ bị
// THU NHỎ về 146x20 — log 21/07/2026; với màn hình đây chỉ còn là lưới an toàn cho
// độ phân giải bất thường.)
//
// Ngưỡng đặt cao hơn mức tối thiểu thật của NVENC một quãng an toàn, vì hai backend
// có ngưỡng khác nhau và ta không muốn dò từng cái.
inline constexpr uint32_t kMinEncodeW = 160;
inline constexpr uint32_t kMinEncodeH = 64;

// Toàn bộ trạng thái của MỘT nguồn. Chứa mutex/atomic nên không copy/move được —
// giữ trong vector<unique_ptr>.
struct SourcePipeline {
    SourcePipeline(uint32_t startBps, uint32_t minBps)
        : curBitrateBps(startBps), rate(startBps, minBps) {}

    // --- Cấu hình, cố định sau khi dựng ---
    uint8_t sourceId = 0;
    HMONITOR monitor = nullptr;
    std::string name;

    ScreenCapture capture;
    InputInjector injector;                        // chỉ thread Recv chạm
    std::unique_ptr<deskhub::HostSession> session; // tạo sau khi biết kích thước nguồn
    deskhub::StreamParams offer;                   // chỉ thread Recv chạm
    deskhub::Packetizer packetizer;                // chỉ thread FrameArrived chạm

    // GĐ7 NACK: kho các datagram video vừa phát, để gửi lại khi client xin. Store từ
    // thread FrameArrived (đường phát), Find từ thread Recv (xử lý NACK) → khoá chung.
    deskhub::RetransmitCache retxCache;
    std::mutex retxMutex;

    // --- Chia sẻ giữa thread FrameArrived và thread Recv ---
    std::atomic<uint32_t> srcW{0}, srcH{0};       // kích thước NÉN (đã làm chẵn)
    std::atomic<uint32_t> srcTexW{0}, srcTexH{0}; // kích thước texture WGC thật
    std::atomic<bool> sizeChanged{false};
    std::atomic<bool> wantFec{false};
    std::atomic<uint32_t> curBitrateBps{0};
    std::atomic<bool> netReady{false};
    // failed = HỎNG THẬT, một chiều: không có backend encoder, capture không start
    // được. Nguồn coi như chết tới hết phiên.
    std::atomic<bool> failed{false};
    // Đã tắt hẳn (người dùng bấm Stop selected, hoặc dọn cuối phiên). Chỉ thread
    // Recv chạm — để shutdownPipeline idempotent, gọi lại lần hai là no-op.
    bool shutdownDone = false;
    // paused = TẠM không encode được (nguồn nhỏ hơn kMinEncode*), HAI CHIỀU. Tách
    // khỏi `failed` vì trước đây gộp chung: cửa sổ thu nhỏ làm ensureEncoder hỏng →
    // failed=true → onFrame thoát ngay ở đầu hàm → không bao giờ thấy cửa sổ mở lại
    // → phiên chết vĩnh viễn dù nguồn đã bình thường trở lại (log 21/07/2026).
    std::atomic<bool> paused{false};
    std::atomic<bool> forceIdr{false};
    std::atomic<uint64_t> peerPacked{0}; // NetAddr::Pack của client hiện tại (0 = chưa có)
    std::atomic<uint64_t> bytesSent{0}, framesSent{0};

    // Số liệu ĐÃ QUY ĐỔI cho giao diện, cập nhật mỗi giây từ chính khối thống kê đã
    // in log (khỏi tính hai lần hai chỗ rồi lệch nhau). Chỉ thread Recv ghi; đọc từ
    // publishRows cùng thread, nhưng để atomic cho thống nhất với phần còn lại.
    std::atomic<uint32_t> uiFps{0}, uiKbps{0}, uiRttMs{0};
    std::atomic<uint32_t> captured{0};
    std::atomic<uint32_t> nextFrameId{0};

    std::mutex encMutex; // bảo vệ encoder + cachedTex giữa 2 thread
    std::unique_ptr<IVideoEncoder> encoder;
    // Dựng encoder theo (kích thước nén, kích thước texture thật). Thread Recv cũng
    // cần gọi (encode lại frame tĩnh khi có yêu cầu IDR) nên phải giữ được sau khi
    // vòng khởi tạo kết thúc. GỌI DƯỚI encMutex.
    std::function<bool(uint32_t, uint32_t, uint32_t, uint32_t)> ensureEncoderFn;

    // WGC chỉ phát frame khi nội dung ĐỔI. Cache frame cuối để khi client xin IDR
    // mà nguồn đang tĩnh (menu, màn hình đứng im) vẫn có cái để encode gửi đi —
    // không cache thì client join màn hình tĩnh sẽ đen vĩnh viễn.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> cachedTex;
    std::atomic<bool> haveCached{false};
    std::atomic<uint64_t> lastFrameUs{0};
    uint64_t lastKeepaliveUs = 0; // lần bơm lại frame cache gần nhất — chỉ thread Recv chạm

    // --- Congestion control, chỉ thread Recv chạm ---
    // Policy thuần ở core; curBitrateBps/wantFec ở trên là bản sao atomic cho thread
    // FrameArrived đọc (nó không được chạm vào rate).
    deskhub::BitrateController rate;

    // --- Thống kê cửa sổ 1s, chỉ thread Recv chạm ---
    uint32_t lastCaptured = 0;
    uint64_t lastBytes = 0, lastFrames = 0;

    // --- Chẩn đoán (docs/09): bộ đếm cửa sổ 1s. Ghi từ thread FrameArrived (và
    // thread Recv lúc encode lại frame tĩnh), đọc-và-reset ở khối thống kê 1s. ---
    std::atomic<uint32_t> dgEncMsSum{0}, dgEncMsMax{0}, dgEncCount{0};
    std::atomic<uint32_t> dgBurstMsMax{0}; // thời gian bắn hết gói của MỘT frame
    std::atomic<uint32_t> dgSendFail{0};   // sendto trả lỗi (buffer gửi đầy...)
    std::atomic<uint32_t> dgIdrCount{0};
    // Sự kiện IDR gần nhất — thread FrameArrived ghi, thread Recv in (giữ I/O
    // ngoài đường nóng, bài học Pacer ở docs/06 §7b). bytes==0 = không có sự kiện;
    // ghi bytes CUỐI CÙNG với release để hai trường kia nhìn thấy trước nó.
    std::atomic<uint64_t> dgIdrBytes{0};
    std::atomic<uint32_t> dgIdrPkts{0}, dgIdrBurstMs{0};

    // Đo thời gian một lần Encode + cộng vào bộ đếm cửa sổ. Gọi từ CẢ HAI thread.
    void DiagEncode(IVideoEncoder* enc, ID3D11Texture2D* tex, bool idr) {
        const uint64_t t0 = NowUs();
        const bool ok = enc->Encode(tex, t0, idr);
        const uint32_t ms = uint32_t((NowUs() - t0) / 1000);
        dgEncMsSum.fetch_add(ms, std::memory_order_relaxed);
        dgEncCount.fetch_add(1, std::memory_order_relaxed);
        DiagAtomicMax(dgEncMsMax, ms);
        // Encode hỏng trên đường keepalive/IDR tĩnh trước giờ bị nuốt im lặng —
        // nguồn tĩnh mà encoder chết là client trắng hình không dấu vết.
        if (!ok)
            std::printf("[DIAG][%s] evt=enc_fail idr=%d ms=%u\n", name.c_str(), idr ? 1 : 0, ms);
    }
};

} // namespace

// Chạy trọn một phiên chia sẻ. CHẶN tới khi mọi nguồn đóng / Ctrl+C / lỗi.
//
// Sáu giai đoạn, đánh dấu bằng các mốc "--- ... ---" bên dưới:
//   1. Kiểm tra đầu vào, chọn GPU, mở socket.
//   2. Dựng SourcePipeline cho từng nguồn.
//   3. Khởi động capture — từ đây các thread FrameArrived bắt đầu chạy.
//   4. ĐỢI frame đầu của từng nguồn: phải biết kích thước thật rồi mới chào được
//      trong HELLO_ACK. Nguồn không phát frame nào trong 10 giây thì bỏ, không kéo
//      cả phiên xuống theo.
//   5. Dựng HostSession + InputInjector cho từng nguồn còn sống.
//   6. Vòng Recv — phần thân chính, chạy tới khi kết thúc.
int RunAgent(std::span<const AgentSource> sources, const AgentOptions& opt, AgentControl& ctl) {
    g_ctrlC.store(false);
    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    if (sources.empty()) {
        std::printf("[Agent] No source selected.\n");
        ctl.OnFailed("no source selected");
        return 1;
    }
    if (sources.size() > deskhub::kMaxSources) {
        std::printf("[Agent] At most %zu sources can be shared at once.\n", deskhub::kMaxSources);
        ctl.OnFailed("too many sources");
        return 1;
    }

    GpuChoice gpu;
    if (!CreateBestDevice({GpuVendor::Nvidia, GpuVendor::Intel, GpuVendor::Amd}, gpu)) {
        std::printf("[Agent] Failed to create D3D11 device.\n");
        ctl.OnFailed("gpu init failed");
        return 1;
    }
    std::wprintf(L"[Agent] GPU: %ls [%ls]\n", gpu.description.c_str(), GpuVendorName(gpu.vendor));
    {
        // Immediate context được dùng từ NHIỀU thread (mỗi nguồn một thread
        // FrameArrived, cộng thread Recv encode lại frame tĩnh) -> bắt buộc bật.
        Microsoft::WRL::ComPtr<ID3D10Multithread> mt;
        if (SUCCEEDED(gpu.device.As(&mt))) mt->SetMultithreadProtected(TRUE);
    }

    // MỘT cổng duy nhất, không có phương án hai. Bản trước dò 64 cổng kế tiếp khi
    // 47777 bận; việc đó đã bỏ vì nó giải quyết sai vấn đề — client chỉ biết gõ IP,
    // nên một host nhảy sang 47778 là một host không ai kết nối tới được. Cổng bận
    // gần như luôn có nghĩa là còn một Deskhub cũ đang chạy, và câu trả lời đúng là
    // nói thẳng để người dùng tắt nó đi.
    UdpSocket sock;
    if (!sock.Open(kDeskhubPort)) {
        // Không MessageBox: hàm này chạy trên thread Recv nền — popup Win32 thô
        // chặn cả vòng lặp. Báo qua OnFailed để giao diện tự hiện lỗi.
        if (sock.lastBindAddrInUse()) {
            std::printf(
                "[Agent] Cannot start sharing: UDP port %u is already in use. "
                "Another Deskhub is probably still running — close it and try again.\n",
                unsigned(kDeskhubPort));
            ctl.OnFailed("udp port 47777 is already in use");
        } else {
            std::printf("[Agent] Cannot start sharing: could not open UDP port %u.\n",
                unsigned(kDeskhubPort));
            ctl.OnFailed("could not open udp port 47777");
        }
        return 1;
    }
    sock.SetRecvTimeout(100);

    // Mở firewall cho gói inbound tới được đây. Thêm rule cần admin; instance thường
    // đã bung UAC trước khi vào đây (xem MainMenuWindow::DoShare) nên tới đây thường
    // là đã elevated. Không thêm được thì chỉ cảnh báo — phiên vẫn chạy, chỉ là
    // client có thể không kết nối được cho tới khi firewall được mở tay.
    if (EnsureHostFirewallRule())
        std::printf("[Agent] Windows Firewall: inbound rule verified (all profiles).\n");
    else
        std::printf(
            "[Agent] Could not add/verify a Windows Firewall rule (needs admin). "
            "If the other machine cannot connect, allow client.exe through Windows "
            "Firewall for the current network.\n");

    std::printf(
        "[Agent] Listening on UDP port %u. On the other machine, open Deskhub"
        " and enter one of:\n",
        unsigned(kDeskhubPort));
    for (const auto& a : ListLocalIPv4())
        std::wprintf(L"    %hs    (%ls)\n", a.ip.c_str(), a.name.c_str());

    const uint32_t startBitrate = opt.bitrateMbps * 1'000'000u;
    const uint32_t maxBitrate = startBitrate;
    // Sàn bitrate: dưới mức này hình nát tới mức vô dụng, thà bỏ frame còn hơn.
    // Đây là tham số `minBps` của BitrateController — nó không bao giờ tụt quá đây.
    const uint32_t minBitrate = 1'000'000u;

    // Cửa sổ quản lý phiên (SessionWindow) do NGƯỜI GỌI cấp và đã mở sẵn — RunAgent
    // chỉ nói chuyện qua `ctl`.
    // Đã nghe được trên kDeskhubPort — frontend chuyển sang trạng thái "đang chia sẻ".
    ctl.OnBound();

    std::vector<std::unique_ptr<SourcePipeline>> pipes;

    // Cấp sourceId tăng dần và KHÔNG tái dùng id của nguồn đã tắt: client còn cầm
    // SOURCE_LIST cũ mà HELLO lại trúng một nguồn mới toanh thì xem nhầm màn hình.
    uint8_t nextSourceId = 0;
    auto makePipeline = [&](const AgentSource& s) -> SourcePipeline* {
        auto p = std::make_unique<SourcePipeline>(startBitrate, minBitrate);
        p->sourceId = nextSourceId++;
        p->monitor = s.monitor;
        p->name = s.name;
        pipes.push_back(std::move(p));
        return pipes.back().get();
    };
    for (const AgentSource& s : sources) makePipeline(s);

    // Nối chuỗi capture→encode→gửi cho MỘT nguồn rồi khởi động capture. Tách thành
    // hàm vì được gọi ở HAI chỗ: các nguồn ban đầu ngay dưới đây, và nguồn thêm
    // giữa phiên khi người dùng bấm Add trên cửa sổ phiên (trong vòng Recv).
    auto startPipeline = [&sock, &gpu, &opt](SourcePipeline* p) {
        // NAL vừa nén xong (thread FrameArrived của nguồn này) -> cắt gói -> UDP.
        auto onPacket = [p, &sock](const uint8_t* data, size_t size, uint64_t tsUs,
                            bool keyframe) {
            if (!p->session || p->session->state() != deskhub::HostSession::State::Streaming) return;
            const uint64_t pp = p->peerPacked.load(std::memory_order_acquire);
            if (!pp) return;
            const NetAddr peer = NetAddr::Unpack(pp);
            p->packetizer.SetSessionId(p->session->sessionId());
            // Packetizer là single-thread (thread này). Thread Recv chỉ đặt ý muốn
            // qua atomic, việc bật/tắt thật diễn ra ở đây — khỏi cần khóa.
            p->packetizer.SetFecEnabled(p->wantFec.load(std::memory_order_relaxed));
            // Đo burst gửi (H2): từ gói đầu tới gói cuối của frame này, và bắt lỗi
            // sendto — trước đây trị trả về bị vứt, buffer gửi đầy là mất gói ngay
            // tại host mà không ai hay.
            const uint64_t sendT0 = NowUs();
            const size_t pkts = p->packetizer.SendFrame(
                std::span<const uint8_t>(data, size), p->nextFrameId++, tsUs, keyframe,
                [p, &sock, &peer](std::span<const uint8_t> d) {
                    if (sock.SendTo(peer, d.data(), d.size()))
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
            // Sự kiện IDR (H1): ghi lại cho thread Recv in — cỡ IDR là con số quyết
            // định chẩn đoán chùm mất gói (docs/06 §7b).
            if (pkts && keyframe) {
                p->dgIdrCount.fetch_add(1, std::memory_order_relaxed);
                p->dgIdrPkts.store(uint32_t(pkts), std::memory_order_relaxed);
                p->dgIdrBurstMs.store(burstMs, std::memory_order_relaxed);
                p->dgIdrBytes.store(uint64_t(size), std::memory_order_release);
            }
        };

        // Tạo encoder nếu chưa có. GỌI DƯỚI encMutex. false = backend không dùng được.
        // `w`/`h` là kích thước NÉN (chẵn); `sw`/`sh` là kích thước texture thật.
        auto ensureEncoder = [p, &gpu, &opt, onPacket](uint32_t w, uint32_t h,
                                 uint32_t sw, uint32_t sh) -> bool {
            if (p->encoder) return true;
            EncoderConfig cfg;
            cfg.width = w;
            cfg.height = h;
            cfg.srcWidth = sw;
            cfg.srcHeight = sh;
            cfg.fps = opt.fps;
            cfg.bitrateBps = p->curBitrateBps.load(std::memory_order_relaxed);
            cfg.outputPath.clear(); // không file — NAL chỉ đi qua onPacket
            cfg.onPacket = onPacket;
            p->encoder = CreateEncoder(gpu.device.Get(), cfg);
            if (!p->encoder) {
                std::printf(
                    "[Agent][%s] No usable encoder backend (NVENC + Media Foundation"
                    " both failed).\n",
                    p->name.c_str());
                p->failed.store(true);
                return false;
            }
            return true;
        };
        p->ensureEncoderFn = ensureEncoder;

        // Đường nóng của nguồn này. CHẠY TRÊN THREAD FrameArrived CỦA WGC — không
        // phải thread Recv. Bốn việc, theo thứ tự:
        //   1. Làm chẵn kích thước (ràng buộc NV12).
        //   2. Phát hiện đổi kích thước → vứt encoder + cache, báo cho thread Recv.
        //   3. Chép frame vào cache (để còn cái mà encode khi nguồn đứng yên).
        //   4. Encode.
        // Giữ encMutex suốt từ bước 2: thread Recv cũng chạm vào encoder và cachedTex
        // khi nó phải encode lại frame tĩnh lúc client xin IDR.
        auto onFrame = [p, &gpu, ensureEncoder](const FrameInfo& fi) {
            p->captured.fetch_add(1, std::memory_order_relaxed);
            if (p->failed.load()) return;

            // NV12 lấy mẫu chroma 2x2 -> bề rộng/cao lẻ làm CreateTexture2D(NV12) trả
            // E_INVALIDARG. Nén ở kích thước chẵn nhỏ hơn; cột/hàng lẻ dư bị cắt.
            const uint32_t encW = fi.width & ~1u, encH = fi.height & ~1u;
            if (!encW || !encH) return;

            std::lock_guard<std::mutex> lk(p->encMutex);

            // Nguồn đổi kích thước (đổi độ phân giải / scale màn hình). Encoder và
            // texture cache đều gắn chặt với kích thước cũ -> vứt
            // cả hai, dựng lại ngay ở frame này. Cờ sizeChanged để thread Recv báo
            // RECONFIG + IDR cho client.
            if (p->srcW.load() != encW || p->srcH.load() != encH) {
                if (p->srcW.load())
                    std::printf(
                        "[Agent][%s] Source resized %ux%u -> %ux%u,"
                        " rebuilding encoder.\n",
                        p->name.c_str(), p->srcW.load(), p->srcH.load(), encW, encH);
                p->srcW.store(encW);
                p->srcH.store(encH);
                p->srcTexW.store(fi.width);
                p->srcTexH.store(fi.height);
                p->encoder.reset();
                p->cachedTex.Reset();
                p->haveCached.store(false, std::memory_order_release);
                p->sizeChanged.store(true, std::memory_order_release);
            }

            // Nguồn nhỏ hơn mức encoder nhận (độ phân giải bất thường). TRẠNG THÁI
            // TẠM, không phải lỗi: bỏ qua frame và giữ nguyên phiên.
            //
            // Chặn ở ĐÚNG chỗ này mới thoát được: trên nó là đoạn ghi nhận kích
            // thước — vẫn phải chạy, vì đó là thứ duy nhất cho ta biết nguồn đã to
            // trở lại. Dưới nó là cache + encode — đều vô nghĩa ở cỡ này. Thoát
            // sớm hơn (như `failed` làm) là tự bịt mắt: không còn đường nhận ra
            // nguồn đã bình thường, phiên treo vĩnh viễn.
            //
            // Không đụng cachedTex: đoạn đổi kích thước ở trên đã haveCached=false,
            // nên nhánh keepalive của thread Recv tự nhiên không bắn — khỏi phải
            // thêm điều kiện ở đó.
            if (encW < kMinEncodeW || encH < kMinEncodeH) {
                if (!p->paused.exchange(true, std::memory_order_acq_rel))
                    std::printf(
                        "[Agent][%s] Source too small to encode (%ux%u) —"
                        " paused, waiting for it to grow back.\n",
                        p->name.c_str(), encW, encH);
                return;
            }
            if (p->paused.exchange(false, std::memory_order_acq_rel))
                std::printf("[Agent][%s] Source back to %ux%u — resuming.\n",
                    p->name.c_str(), encW, encH);

            // Lưu bản SAO của frame cuối. Bắt buộc phải copy chứ không giữ con trỏ:
            // texture của WGC chỉ sống trong phạm vi callback (xem CaptureTypes.h).
            // Đây là thứ cứu trường hợp nguồn đứng yên — xem "cache frame cuối" ở
            // đầu file. Texture cache tạo lười, một lần, rồi CopyResource mỗi frame.
            if (!p->cachedTex) {
                D3D11_TEXTURE2D_DESC d{};
                fi.texture->GetDesc(&d);
                d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
                d.MiscFlags = 0;
                d.Usage = D3D11_USAGE_DEFAULT;
                d.CPUAccessFlags = 0;
                if (FAILED(gpu.device->CreateTexture2D(&d, nullptr, p->cachedTex.GetAddressOf())))
                    p->cachedTex.Reset();
            }
            if (p->cachedTex) {
                gpu.context->CopyResource(p->cachedTex.Get(), fi.texture);
                p->haveCached.store(true, std::memory_order_release);
            }
            p->lastFrameUs.store(NowUs(), std::memory_order_relaxed);

            // Chặn ở đây chứ không sớm hơn: mọi bước trên (cache frame, ghi nhận
            // kích thước) vẫn phải chạy TRƯỚC khi có client, vì giai đoạn 4 của
            // RunAgent đang đợi đúng srcW để dựng offer.
            if (!p->netReady.load(std::memory_order_acquire)) return;
            if (!ensureEncoder(encW, encH, fi.width, fi.height)) return;
            // Encode liên tục kể cả khi chưa có client (đơn giản, VBV ổn định);
            // NAL bị bỏ ở onPacket nếu chưa STREAMING. DiagEncode = Encode + đo
            // thời gian cho cửa sổ chẩn đoán (H1).
            p->DiagEncode(p->encoder.get(), fi.texture, p->forceIdr.exchange(false));
        };

        if (!p->capture.Start(p->monitor, gpu.device.Get(), onFrame)) {
            std::printf("[Agent][%s] Failed to start capture — skipping this source.\n",
                p->name.c_str());
            p->failed.store(true);
        }
    };

    // Tắt hẳn MỘT nguồn: nút Stop selected, nguồn thêm vào mà không lên hình,
    // hoặc dọn dẹp cuối phiên. Idempotent (shutdownDone) — dọn cuối phiên gọi lại
    // trên nguồn đã tắt giữa chừng là no-op. CHỈ gọi từ thread Recv.
    auto shutdownPipeline = [&sock](SourcePipeline* p) {
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
    };

    // --- Khởi động capture cho từng nguồn ---
    for (auto& up : pipes) startPipeline(up.get());

    // --- Đợi frame đầu của từng nguồn để biết kích thước (offer trong HELLO_ACK) ---
    for (int i = 0; i < 1000 && !g_ctrlC.load() && !ctl.stopRequested(); ++i) {
        bool allKnown = true;
        for (auto& p : pipes)
            if (!p->failed.load() && !p->srcW.load() && !p->capture.Closed()) allKnown = false;
        if (allKnown) break;
        Sleep(10);
    }

    // Nguồn không phát frame nào trong 10s thì bỏ, không kéo cả phiên xuống theo.
    std::vector<SourcePipeline*> live;
    for (auto& p : pipes) {
        if (p->failed.load() || !p->srcW.load()) {
            if (!p->failed.load())
                std::printf("[Agent][%s] No frame within 10s — not sharing this source.\n",
                    p->name.c_str());
            shutdownPipeline(p.get());
            continue;
        }
        live.push_back(p.get());
    }
    if (live.empty()) {
        std::printf("[Agent] No usable source — stopping.\n");
        ctl.OnFailed("no usable source");
        return 1;
    }

    // --- Dựng phiên + injector cho từng nguồn còn sống ---
    // Tách thành hàm vì cũng được gọi ở HAI chỗ: các nguồn ban đầu ngay dưới, và
    // nguồn thêm giữa phiên (trong vòng Recv, khi frame đầu của nó về).
    NetAddr replyAddr; // địa chỉ nguồn của gói đang xử lý (chỉ thread Recv dùng)

    // Máy này trả lời các câu hỏi TRƯỚC KHI có phiên — "đang chia sẻ gì?"
    // (LIST_SOURCES), "còn sống? bao xa?" (PING sessionId=0). Beacon chỉ DỰNG byte
    // trả lời; gửi là việc của vòng Recv, về đúng nơi vừa hỏi. (Nhánh DISCOVER/
    // ANNOUNCE đã gỡ 2026-07-27 cùng toàn bộ LAN discovery — chỉ dùng địa chỉ tay.)
    deskhub::Beacon beacon;
    uint8_t beaconBuf[deskhub::kMaxDatagram]; // riêng, không dùng chung với buf nhận:
                                              // Reply() đọc gói đến trong lúc ghi ra

    auto attachSession = [&](SourcePipeline* p) {
        p->offer.width = uint16_t(p->srcW.load());
        p->offer.height = uint16_t(p->srcH.load());
        p->offer.fps = uint8_t(opt.fps);
        p->offer.bitrateBps = startBitrate;
        std::printf("[Agent] Source %u \"%s\": %ux%u @%ufps, %u Mbps.\n",
            p->sourceId, p->name.c_str(), p->offer.width, p->offer.height,
            opt.fps, opt.bitrateMbps);

        // Chuột/bàn phím LUÔN được chia sẻ (chốt 2026-07-27) — chỉ còn một đường:
        // Init được thì bật, không thì thôi.
        p->injector.SetEnabled(p->injector.Init(p->monitor));

        deskhub::HostCallbacks cb;
        cb.send = [&sock, &replyAddr](std::span<const uint8_t> d) {
            sock.SendTo(replyAddr, d.data(), d.size());
        };
        // Nguồn ngẫu nhiên mã hoá cho sessionId (hàng rào chặn gói giả trên UDP).
        // core/ không đụng được API hệ điều hành nên phải nối từ đây. Thiếu callback
        // này thì HostSession từ chối MỌI kết nối (fail closed) — xem BeginSession.
        cb.randomBytes = [](std::span<uint8_t> out) {
            return RandomBytes(out.data(), out.size());
        };
        cb.onStart = [p] {
            p->forceIdr.store(true); // IDR mở màn (kèm SPS/PPS — repeatSPSPPS=1)
            std::printf("[Agent][%s] Client START — beginning video push.\n", p->name.c_str());
        };
        cb.onKeyframeRequest = [p] { p->forceIdr.store(true); };
        // GĐ7: client xin gửi lại các mảnh còn thiếu của một frame. Tra kho rồi phát
        // lại thẳng cho peer hiện tại. Rẻ hơn nhiều so với ép cả một IDR, và chỉ tốn
        // băng thông đúng lúc thật sự mất gói. Chạy trên thread Recv.
        cb.onNack = [p, &sock](uint32_t frameId, std::span<const uint16_t> indices) {
            const uint64_t pp = p->peerPacked.load(std::memory_order_acquire);
            if (!pp) return;
            const NetAddr peer = NetAddr::Unpack(pp);
            std::lock_guard<std::mutex> lk(p->retxMutex);
            for (uint16_t idx : indices) {
                const auto d = p->retxCache.Find(frameId, idx);
                if (!d.empty()) sock.SendTo(peer, d.data(), d.size());
            }
        };
        cb.onInput = [p](const deskhub::InputEvent& e) { p->injector.Apply(e); };
        // SET_FOCUS(false): client rời nguồn này (đổi màn hình xem / app xuống nền)
        // giữa lúc có thể đang giữ phím — nhả hết, không để kẹt. Nhánh true không
        // còn việc gì: nguồn là cả màn hình, không có cửa sổ nào để kéo lên trước.
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
            std::printf("[Agent][%s] Client left (BYE/timeout).\n", p->name.c_str());
        };
        // GD5 congestion control, RIÊNG từng nguồn: hai nguồn có thể đi cùng một
        // đường mạng nhưng bitrate của chúng độc lập nhau, và client có thể chỉ
        // đang xem một trong hai.
        // GD5 congestion control: policy nằm ở deskhub::BitrateController (core, test
        // được offline). Ở đây chỉ còn phần dính thiết bị — đẩy quyết định xuống
        // encoder, cập nhật atomic cho thread FrameArrived, và in log.
        cb.onFeedback = [p](const deskhub::Feedback& fb) {
            // RTT chỉ đo được ở PHÍA CLIENT (nó phát PING và trừ khi PONG về), nên
            // FEEDBACK là đường duy nhất con số đó tới được host — và màn chia sẻ
            // cần nó để hiện "máy đang xem cách bao xa".
            p->uiRttMs.store(fb.rttMs, std::memory_order_relaxed);

            const deskhub::BitrateDecision d = p->rate.Update(fb, NowUs());

            if (d.fecToggled) {
                p->wantFec.store(d.fecEnabled, std::memory_order_relaxed);
                if (d.fecEnabled)
                    std::printf("[Agent][%s] FEC on (loss %u%%).\n", p->name.c_str(), fb.lossPct);
                else
                    std::printf("[Agent][%s] FEC off (link clean).\n", p->name.c_str());
            }

            if (!d.changeBitrate) return;

            const uint32_t cur = p->rate.bitrateBps();
            std::lock_guard<std::mutex> lk(p->encMutex);
            // Encoder từ chối thì KHÔNG commit: lần Feedback sau tính lại từ mức cũ.
            if (p->encoder && p->encoder->SetBitrate(d.bitrateBps)) {
                p->rate.CommitBitrate(d.bitrateBps);
                p->curBitrateBps.store(d.bitrateBps, std::memory_order_relaxed);
                std::printf("[Agent][%s] Bitrate %.1f -> %.1f Mbps (loss %u%%, RTT %u ms)\n",
                    p->name.c_str(), cur / 1e6, d.bitrateBps / 1e6, fb.lossPct, fb.rttMs);
            }
        };

        p->session = std::make_unique<deskhub::HostSession>(cb, p->offer);
        p->netReady.store(true, std::memory_order_release);
    };
    for (SourcePipeline* p : live) attachSession(p);

    // Danh sách nguồn CHỐT ở đây và không đổi nữa (2026-07-27): phiên chia sẻ TẤT CẢ
    // màn hình đang gắn, không có nút thêm/bớt giữa chừng. Bản trước có nút Add/Stop
    // selected và một hàng đợi `pendingAdds` cho nguồn đang đợi frame đầu — cả hai đã
    // bỏ. Cắm thêm màn hình giữa phiên thì phải dừng và share lại.

    // Đẩy danh sách nguồn hiện tại cho cửa sổ phiên. SetRows tự so sánh với lần
    // trước nên gọi lại mỗi giây cũng không làm listbox nhấp nháy.
    auto publishRows = [&] {
        std::vector<SessionSourceRow> rows;
        std::vector<deskhub::SourceInfo> infos; // ảnh chụp cho Beacon, dựng cùng lượt
        for (SourcePipeline* p : live) {
            if (p->failed.load() || p->capture.Closed()) continue;
            const uint32_t w = p->srcW.load(), hgt = p->srcH.load();
            const uint64_t peer = p->peerPacked.load(std::memory_order_relaxed);

            SessionSourceRow r;
            r.sourceId = p->sourceId;
            wchar_t suffix[64];
            swprintf(suffix, 64, L"  (%ux%u%ls)", w, hgt,
                peer ? L", viewer connected" : L"");
            r.label = FromUtf8(p->name) + suffix;
            // Các trường CÓ CẤU TRÚC (GĐ9): giao diện vẽ mỗi mẩu một chỗ (tên đậm,
            // độ phân giải mono, huy hiệu trạng thái) — `label` giữ cho log.
            r.name = FromUtf8(p->name);
            r.width = w;
            r.height = hgt;
            r.viewerConnected = peer != 0;
            if (peer) r.viewerAddr = NetAddr::Unpack(peer).ToString();
            r.fps = p->uiFps.load(std::memory_order_relaxed);
            r.kbps = p->uiKbps.load(std::memory_order_relaxed);
            r.rttMs = p->uiRttMs.load(std::memory_order_relaxed);
            r.monitor = uint64_t(uintptr_t(p->monitor));
            rows.push_back(std::move(r));

            deskhub::SourceInfo si;
            si.sourceId = p->sourceId;
            si.width = uint16_t(w);
            si.height = uint16_t(hgt);
            si.name = p->name;
            infos.push_back(std::move(si));
        }
        // Beacon trả lời trên CÙNG thread Recv này nên cập nhật thẳng, không khoá.
        beacon.SetSources(infos);
        ctl.SetRows(std::move(rows));
    };
    publishRows();

    // "Host thắng" khi hai bên cùng điều khiển: theo dõi chuột/phím VẬT LÝ của
    // người ngồi máy (hook LL trên thread riêng, lọc cờ injected để không tự tính
    // input mình bơm); InputInjector::Apply thấy mốc còn mới là bỏ qua input từ
    // xa ~1s. Sống theo scope RunAgent (dtor tự Stop trên mọi đường ra).
    LocalInputMonitor localInputMon;
    localInputMon.Start();

    {
        // UIPI nuốt SendInput IM LẶNG khi đích chạy ở mức toàn vẹn cao hơn (game có
        // anti-cheat thường chạy admin). Không có dòng này thì triệu chứng "gõ không
        // ăn" nhìn y hệt lỗi mạng/tiêu điểm — xem ElevatedShare.h.
        const bool elevated = IsProcessElevated();
        std::printf("[Agent] Client control allowed (mouse + keyboard). Host elevated: %s%s\n",
            elevated ? "YES" : "NO",
            elevated ? "" : " — input will NOT reach apps running as administrator");
    }
    std::printf("[Agent] Sharing %zu source(s). Waiting for client...\n", live.size());

    // --- Vòng Recv (thread chính), dùng chung cho mọi nguồn ---
    //
    // Mỗi vòng làm bốn việc, theo thứ tự:
    //   1. Kiểm tra điều kiện dừng (Ctrl+C, hoặc MỌI nguồn đã đóng).
    //   2. recvfrom — chặn tối đa 100 ms, nên vòng lặp luôn quay đủ nhanh để Tick.
    //   3. Định tuyến gói vừa nhận về đúng SourcePipeline (xem sơ đồ đầu file).
    //   4. Tick mọi phiên + in thống kê mỗi giây.
    //
    // Một màn hình bị ngắt KHÔNG giết cả phiên: chỉ dừng khi hết nguồn sống.
    uint8_t buf[deskhub::kMaxDatagram];
    uint64_t lastStatUs = NowUs();
    bool anyFailed = false;
    // H3: thời gian BẬN dài nhất của một vòng Recv trong cửa sổ 1s (không tính lúc
    // chờ recvfrom). Vòng này mà nghẽn thì buffer UDP của kernel gánh — tràn là mất
    // gói thật. Chỉ thread Recv chạm nên không cần atomic.
    uint32_t dgLoopBusyMaxMs = 0;

    for (;;) {
        if (g_ctrlC.load()) break;
        if (ctl.stopRequested()) break; // nút Stop sharing / đóng cửa sổ phiên

        // Hết nguồn sống là hết phiên. Bản trước còn giữ phiên chạy khi cửa sổ phiên
        // còn mở, vì người dùng còn nút Add để thêm nguồn mới; nút đó đã bỏ nên một
        // phiên không còn màn hình nào chỉ là một cửa sổ trống.
        bool anyAlive = false;
        for (SourcePipeline* p : live)
            if (!p->failed.load() && !p->capture.Closed()) anyAlive = true;
        if (!anyAlive) break;

        NetAddr from;
        const int n = sock.RecvFrom(buf, sizeof(buf), from);
        const uint64_t now = NowUs();
        if (n < 0) {
            std::printf("[Agent] Socket error — stopping.\n");
            ctl.OnFailed("socket error");
            anyFailed = true;
            break;
        }

        if (n > 0) {
            const auto span = std::span<const uint8_t>(buf, size_t(n));
            const auto h = deskhub::ParseCommonHeader(span);
            // Hỏi-đáp KHÔNG thuộc phiên nào (DISCOVER, LIST_SOURCES, PING dò đường)
            // đi trước và trả lời về ĐÚNG `from` — chúng tới từ địa chỉ bất kỳ trong
            // mạng, không phải peer của phiên. Beacon dựng byte, ta gửi.
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
                        if (p->session->sessionId() == h->sessionId) dst = p;
                }
                if (dst && !dst->failed.load() && dst->session->HandlePacket(span, now)) {
                    // Gói hợp lệ thuộc phiên — cập nhật peer (client đổi IP/port).
                    const uint64_t pk = from.Pack();
                    if (dst->peerPacked.load(std::memory_order_relaxed) != pk) {
                        dst->peerPacked.store(pk, std::memory_order_release);
                        std::printf("[Agent][%s] Peer: %s\n", dst->name.c_str(),
                            from.ToString().c_str());
                    }
                }
            }
        }

        for (SourcePipeline* p : live) {
            if (p->failed.load()) continue;
            p->session->Tick(now);

            // Sự kiện IDR do thread FrameArrived ghi lại (H1) — in ở đây để I/O
            // không nằm trên đường nóng. Luôn bật: IDR hiếm và cỡ của nó là con số
            // chẩn đoán quan trọng nhất phía host.
            if (const uint64_t ib = p->dgIdrBytes.exchange(0, std::memory_order_acquire)) {
                std::printf("[DIAG][%s] evt=idr bytes=%llu pkts=%u burst_ms=%u\n",
                    p->name.c_str(), (unsigned long long)ib,
                    p->dgIdrPkts.load(std::memory_order_relaxed),
                    p->dgIdrBurstMs.load(std::memory_order_relaxed));
            }

            // Nguồn vừa đổi kích thước (thread FrameArrived đã dựng lại encoder).
            // Báo client kích thước mới + IDR: stream đổi SPS giữa chừng, không có
            // IDR thì decoder client chỉ có rác cho tới keyframe kế tiếp.
            // `paused` đứng TRƯỚC exchange là cố ý: short-circuit giữ nguyên cờ
            // sizeChanged trong lúc tạm dừng. Client không phải dựng lại decoder cho
            // một cỡ suy biến (146x20) rồi lát nữa dựng lại lần nữa; và lúc nguồn to
            // trở lại, onFrame set cờ lần nữa nên RECONFIG vẫn được gửi đúng cỡ mới.
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

            // Nguồn đang TĨNH (WGC chỉ phát frame khi nội dung đổi) — encoder không
            // có input mới. Hai nhu cầu gộp chung một chỗ:
            //   1. Yêu cầu IDR đang treo (>200ms không FrameArrived) → encode lại
            //      frame cache với IDR, không thì client join màn hình tĩnh đen mãi.
            //   2. KEEPALIVE ~2fps: MFT async (QSV) NGẬM output tới khi có input kế
            //      tiếp — nguồn đứng im là frame cuối kẹt trong encoder với timestamp
            //      cũ, client thấy nội dung trễ một nhịp và e2e ảo tới hàng chục giây
            //      (đo 2026-07-21). Bơm lại frame cache để đẩy nó ra; nội dung không
            //      đổi nên P-frame chỉ vài KB, chi phí không đáng kể.
            const uint64_t sinceFrameUs = now - p->lastFrameUs.load(std::memory_order_relaxed);
            const bool wantIdrFlush = p->forceIdr.load() && sinceFrameUs > 200'000;
            const bool wantKeepalive = sinceFrameUs > 500'000 &&
                                       now - p->lastKeepaliveUs >= 500'000;
            if (p->session->state() == deskhub::HostSession::State::Streaming &&
                p->haveCached.load(std::memory_order_acquire) &&
                (wantIdrFlush || wantKeepalive)) {
                std::lock_guard<std::mutex> lk(p->encMutex);
                if (p->ensureEncoderFn(p->srcW.load(), p->srcH.load(),
                        p->srcTexW.load(), p->srcTexH.load())) {
                    p->DiagEncode(p->encoder.get(), p->cachedTex.Get(),
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
                // `applied` là thống kê MẠNG (event tới nơi và được giao cho injector),
                // KHÔNG phải bằng chứng phím đã tới ứng dụng. Injector còn vứt tiếp ở
                // cổng tiêu điểm — `skipped` là con số duy nhất lộ ra chuyện đó. Thiếu
                // nó thì "gõ không ăn" không phân biệt được với "không nhận được gói".
                std::printf(
                    "[Agent][%s] %-9s | capture %.0f fps | send %.0f fps, %.0f kbps"
                    " | input %llu (lost %llu, skipped %llu)\n",
                    p->name.c_str(), StateName(p->session->state()),
                    (cap - p->lastCaptured) / secs,
                    (fr - p->lastFrames) / secs,
                    (by - p->lastBytes) * 8.0 / 1000.0 / secs,
                    (unsigned long long)ist.applied,
                    (unsigned long long)ist.lost,
                    (unsigned long long)p->injector.skipped());
                // Cùng phép tính vừa in ra log, giữ lại cho giao diện (panel "máy
                // đang xem" của màn chia sẻ). Tính lại ở publishRows sẽ cho hai con
                // số khác nhau vì hai chỗ đó chốt bộ đếm ở hai thời điểm khác nhau.
                p->uiFps.store(uint32_t((fr - p->lastFrames) / secs + 0.5),
                    std::memory_order_relaxed);
                p->uiKbps.store(uint32_t((by - p->lastBytes) * 8.0 / 1000.0 / secs + 0.5),
                    std::memory_order_relaxed);

                p->lastCaptured = cap;
                p->lastBytes = by;
                p->lastFrames = fr;

                // Dòng chẩn đoán 1s của nguồn này (H1+H2): đọc-và-reset bộ đếm
                // cửa sổ.
                {
                    const uint32_t ec = p->dgEncCount.exchange(0, std::memory_order_relaxed);
                    const uint32_t es = p->dgEncMsSum.exchange(0, std::memory_order_relaxed);
                    const uint32_t em = p->dgEncMsMax.exchange(0, std::memory_order_relaxed);
                    std::printf(
                        "[DIAG][%s] evt=sum enc_ms_avg=%.1f enc_ms_max=%u idr=%u"
                        " burst_ms_max=%u send_fail=%u\n",
                        p->name.c_str(), ec ? double(es) / ec : 0.0, em,
                        p->dgIdrCount.exchange(0, std::memory_order_relaxed),
                        p->dgBurstMsMax.exchange(0, std::memory_order_relaxed),
                        p->dgSendFail.exchange(0, std::memory_order_relaxed));
                }
            }
            // Sức khỏe thread Recv (H3), chung cho mọi nguồn.
            std::printf("[DIAG][agent] evt=sum loop_busy_ms_max=%u\n", dgLoopBusyMaxMs);
            dgLoopBusyMaxMs = 0;
            // Làm mới danh sách trên cửa sổ phiên theo nhịp 1s: bắt các thay đổi
            // không đi qua lệnh của người dùng (nguồn đổi độ phân giải, màn hình
            // bị rút, client vào/ra). SetRows tự bỏ qua khi không đổi.
            publishRows();
            lastStatUs = now;
        }

        // H3: vòng này bận bao lâu (từ lúc recvfrom trả về tới đây). Nghẽn nặng thì
        // báo ngay, không đợi cửa sổ 1s.
        const uint32_t busyMs = uint32_t((NowUs() - now) / 1000);
        if (busyMs > dgLoopBusyMaxMs) dgLoopBusyMaxMs = busyMs;
        if (busyMs > 250)
            std::printf("[DIAG][agent] evt=recv_stall busy_ms=%u\n", busyMs);
    }

    // --- Dọn dẹp ---
    // Quét TOÀN BỘ pipes chứ không riêng live: nguồn đang chờ frame đầu và nguồn
    // đã tắt giữa phiên đều nằm ngoài live; shutdownPipeline idempotent nên nguồn
    // nào dọn rồi chỉ là no-op, còn tổng kết thì tính cả nguồn đã tắt giữa chừng.
    uint64_t totalFrames = 0;
    double totalMB = 0;
    for (auto& up : pipes) {
        shutdownPipeline(up.get());
        totalFrames += up->framesSent.load();
        totalMB += up->bytesSent.load() / 1e6;
    }
    // Không đóng `ctl` ở đây — người gọi sở hữu nó (SessionWindow.Stop).
    std::printf("[Agent] Stopped. Total: %llu frames sent, %.2f MB.\n",
        (unsigned long long)totalFrames, totalMB);
    SetConsoleCtrlHandler(CtrlHandler, FALSE);
    return anyFailed ? 1 : 0;
}
