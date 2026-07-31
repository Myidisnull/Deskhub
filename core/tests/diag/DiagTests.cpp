// =============================================================================
// DiagTests.cpp — test cho tầng diag/: bộ đếm cửa sổ và phép dựng dòng log.
//
// VÌ SAO PHẦN NÀY ĐÁNG TEST
//   Trước khi gom về core, toàn bộ số liệu chẩn đoán nằm rải trong năm client và
//   KHÔNG có một test nào — sai ở đây thì hậu quả là chẩn đoán sai, thứ chỉ lộ ra
//   khi đang bận đi tìm một lỗi khác. Hai tính chất dưới đây là thứ dễ hỏng nhất
//   và giờ được khoá lại bằng test:
//     - TakeReset() phải THẬT SỰ xoá: cửa sổ sau không được thừa đỉnh của cửa sổ
//       trước, và max không bao giờ bị một mẫu nhỏ hơn hạ xuống.
//     - Trường theo nền phải BIẾN MẤT khi nền không có, chứ không in số 0 —
//       "present_ms=0.0/0" trên Ubuntu sẽ bị đọc thành "Present tức thời".
//
// LIÊN QUAN: deskhub/diag/WindowStat.h, ClientDiag.h, AgentDiag.h
// =============================================================================
#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/diag/AgentDiag.h"
#include "deskhub/diag/ClientDiag.h"

#include <cstdio>
#include <cstring>

using namespace deskhub;
using namespace deskhub::diag;

namespace {

bool Has(const char* haystack, const char* needle) {
    return std::strstr(haystack, needle) != nullptr;
}

void TestWindowStat() {
    std::printf("[diag] WindowStat: avg/max/count và tính chất đọc-và-xoá...\n");
    WindowStat s;
    WindowStat::Snapshot e = s.TakeReset();
    Check(e.count == 0 && e.max == 0 && e.avg == 0.0, "WindowStat: cửa sổ rỗng cho toàn số 0");

    s.Add(10);
    s.Add(20);
    s.Add(30);
    e = s.TakeReset();
    Check(e.count == 3, "WindowStat: đếm đủ mẫu");
    Check(e.max == 30, "WindowStat: max là mẫu lớn nhất");
    Check(e.avg > 19.9 && e.avg < 20.1, "WindowStat: avg = tổng/đếm");

    // Tính chất quan trọng nhất: đỉnh của cửa sổ trước KHÔNG được rò sang sau.
    e = s.TakeReset();
    Check(e.count == 0 && e.max == 0, "WindowStat: TakeReset xoá sạch cửa sổ");
    s.Add(1);
    e = s.TakeReset();
    Check(e.max == 1, "WindowStat: cửa sổ mới không thừa max cũ");

    // max không bao giờ bị một mẫu nhỏ hơn ghi đè.
    s.Add(50);
    s.Add(5);
    e = s.TakeReset();
    Check(e.max == 50, "WindowStat: mẫu nhỏ không hạ được max");
}

void TestCountMaxMin() {
    std::printf("[diag] WindowCount / WindowMax / RunningMin...\n");
    WindowCount c;
    Check(c.TakeReset() == 0, "WindowCount: rỗng là 0");
    c.Add();
    c.Add(4);
    Check(c.peek() == 5, "WindowCount: peek không xoá");
    Check(c.TakeReset() == 5, "WindowCount: cộng dồn đúng");
    Check(c.TakeReset() == 0, "WindowCount: đọc-và-xoá");

    WindowMax m;
    m.Add(7);
    m.Add(3);
    Check(m.TakeReset() == 7, "WindowMax: giữ giá trị lớn nhất");
    Check(m.TakeReset() == 0, "WindowMax: đọc-và-xoá");

    RunningMin r;
    Check(r.value() == 0, "RunningMin: 0 = chưa có mẫu");
    r.Add(0); // 0 không phải mẫu hợp lệ, phải bị bỏ qua
    Check(r.value() == 0, "RunningMin: bỏ qua mẫu 0");
    r.Add(900);
    r.Add(1200);
    r.Add(400);
    Check(r.value() == 400, "RunningMin: giữ nhỏ nhất từng thấy");
    // KHÔNG reset theo cửa sổ — đây là sàn mạng cả phiên.
    Check(r.value() == 400, "RunningMin: đọc không xoá");
}

// Dựng một LinkWindow đủ trường để kiểm tra phần số liệu đường truyền.
LinkWindow MakeWindow() {
    LinkWindow w;
    w.secs = 1.0;
    w.fps = 59.0;
    w.kbps = 8000.0;
    w.lossPct = 2.5;
    w.framesDropped = 3;
    w.packetsRecovered = 11;
    w.latePackets = 7;
    w.lateMsAvg = 42.0;
    w.lateMsMax = 88;
    return w;
}

void TestClientSum() {
    std::printf("[diag] client evt=sum: trường theo nền, đọc-và-xoá...\n");
    const LinkWindow w = MakeWindow();
    char buf[ClientDiag::kSumBufBytes];

    // Ubuntu: không present_ms, không disp_drop.
    {
        ClientDiag d;
        d.asmMs.Add(4);
        d.asmMs.Add(6);
        d.decMs.Add(12);
        d.dqDrop.Add(2);
        d.dispDrop.Add(9); // nền này không in — bộ đếm vẫn chạy, chỉ không lộ ra
        d.loopBusyMs.Add(31);
        d.minRttUs.Add(1500);

        d.FormatSum(buf, sizeof(buf), "01:02:03", w, 25, 17'000);
        Check(Has(buf, "[DIAG] evt=sum t=01:02:03"), "client evt=sum: có tiền tố và mốc giờ");
        Check(Has(buf, "asm_ms=5.0/6"), "client evt=sum: asm_ms avg/max");
        Check(Has(buf, "dec_ms=12.0/12"), "client evt=sum: dec_ms avg/max");
        Check(!Has(buf, "present_ms"), "client evt=sum: Ubuntu KHÔNG có present_ms");
        Check(!Has(buf, "disp_drop"), "client evt=sum: Ubuntu KHÔNG có disp_drop");
        Check(Has(buf, "dq_drop=2"), "client evt=sum: dq_drop");
        Check(Has(buf, "late=7 late_ms_avg=42 late_ms_max=88"), "client evt=sum: gói về muộn");
        Check(Has(buf, "gap_ms_max=25"), "client evt=sum: gap_ms_max truyền từ Reassembler");
        Check(Has(buf, "loop_busy_ms_max=31"), "client evt=sum: sức khoẻ vòng Net");
        Check(Has(buf, "min_rtt_ms=1.5"), "client evt=sum: sàn RTT theo ms");
        Check(Has(buf, "e2e_ms=17.0"), "client evt=sum: e2e theo ms");

        // Gọi lần hai không có mẫu mới: mọi bộ đếm cửa sổ phải về 0.
        d.FormatSum(buf, sizeof(buf), "01:02:04", w, 0, -1);
        Check(Has(buf, "asm_ms=0.0/0") && Has(buf, "dq_drop=0") && Has(buf, "loop_busy_ms_max=0"),
            "client evt=sum: FormatSum đọc-và-xoá bộ đếm");
        Check(Has(buf, "e2e_ms=0.0"), "client evt=sum: e2e âm (chưa có mẫu) in ra 0");
        Check(Has(buf, "min_rtt_ms=1.5"), "client evt=sum: min_rtt KHÔNG bị xoá theo cửa sổ");
    }

    // Windows: có present_ms, không disp_drop.
    {
        ClientDiag d(ClientDiagCaps{/*presentMs=*/true, /*dispDrop=*/false});
        d.presentMs.Add(16);
        d.FormatSum(buf, sizeof(buf), "01:02:03", w, 0, 0);
        Check(Has(buf, "present_ms=16.0/16"), "client evt=sum: Windows có present_ms");
        Check(!Has(buf, "disp_drop"), "client evt=sum: Windows KHÔNG có disp_drop");
    }

    // Apple + Android: có disp_drop, không present_ms.
    {
        ClientDiag d(ClientDiagCaps{/*presentMs=*/false, /*dispDrop=*/true});
        d.dispDrop.Add(4);
        d.FormatSum(buf, sizeof(buf), "01:02:03", w, 0, 0);
        Check(Has(buf, "disp_drop=4"), "client evt=sum: Apple/Android có disp_drop");
        Check(!Has(buf, "present_ms"), "client evt=sum: Apple/Android KHÔNG có present_ms");
    }
}

void TestClientStatus() {
    std::printf("[diag] client: dòng trạng thái mỗi giây...\n");
    const LinkWindow w = MakeWindow();
    char buf[ClientDiag::kStatusBufBytes];
    ClientDiag::FormatStatus(buf, sizeof(buf), "08:30:00", w, 2400, 17'000);
    Check(Has(buf, "[Client t=08:30:00]"), "client status: mốc giờ trong tiền tố");
    Check(Has(buf, "59 fps"), "client status: fps");
    Check(Has(buf, "dropped 3 frame"), "client status: frame bị bỏ");
    Check(Has(buf, "lost  2.5% pkts"), "client status: % mất gói, bề rộng cố định");
    Check(Has(buf, "fec+11"), "client status: gói FEC cứu được");
    Check(Has(buf, "RTT 2.4 ms"), "client status: RTT mới nhất theo ms");
    Check(Has(buf, "e2e ~17.0 ms"), "client status: e2e");

    // e2e chưa có mẫu (-1) phải in 0, không phải số âm.
    ClientDiag::FormatStatus(buf, sizeof(buf), "08:30:01", w, 0, -1);
    Check(Has(buf, "e2e ~0.0 ms"), "client status: e2e chưa có mẫu in ra 0");
}

void TestSourceDiag() {
    std::printf("[diag] host evt=sum: encode/gửi theo từng nguồn...\n");
    char buf[SourceDiag::kSumBufBytes];

    // Windows: không cap_idle, không zerocopy.
    {
        SourceDiag s;
        s.encMs.Add(2);
        s.encMs.Add(4);
        s.encLatMs.Add(30);
        s.idr.Add();
        s.sendFail.Add(2);
        s.burstMs.Add(9);
        s.FormatSum(buf, sizeof(buf), "01:02:03", "Screen 1", 5, true);
        Check(Has(buf, "[DIAG][Screen 1] evt=sum t=01:02:03"), "source evt=sum: tiền tố + giờ");
        Check(Has(buf, "enc_ms_avg=3.0 enc_ms_max=4"), "source evt=sum: enc_ms");
        Check(Has(buf, "enc_lat_ms=30.0/30"), "source evt=sum: độ trễ thật của bộ nén");
        Check(!Has(buf, "cap_idle"), "source evt=sum: Windows KHÔNG có cap_idle");
        Check(!Has(buf, "zerocopy"), "source evt=sum: Windows KHÔNG có zerocopy");
        Check(Has(buf, "idr=1 burst_ms_max=9 send_fail=2"), "source evt=sum: gửi + IDR");

        s.FormatSum(buf, sizeof(buf), "01:02:04", "Screen 1", 0, false);
        Check(Has(buf, "enc_ms_avg=0.0 enc_ms_max=0") && Has(buf, "idr=0 burst_ms_max=0"),
            "source evt=sum: FormatSum đọc-và-xoá bộ đếm");
    }

    // macOS có cap_idle; Ubuntu có zerocopy.
    {
        SourceDiag mac(AgentDiagCaps{/*capIdle=*/true, /*zerocopy=*/false});
        mac.FormatSum(buf, sizeof(buf), "01:02:03", "Built-in", 42, true);
        Check(Has(buf, "cap_idle=42"), "source evt=sum: macOS có cap_idle");
        Check(!Has(buf, "zerocopy"), "source evt=sum: macOS KHÔNG có zerocopy");

        SourceDiag ubu(AgentDiagCaps{/*capIdle=*/false, /*zerocopy=*/true});
        ubu.FormatSum(buf, sizeof(buf), "01:02:03", "HDMI-1", 42, false);
        Check(Has(buf, "zerocopy=0"), "source evt=sum: Ubuntu có zerocopy, in đúng 0");
        Check(!Has(buf, "cap_idle"), "source evt=sum: Ubuntu KHÔNG có cap_idle");
    }
}

void TestSourceIdr() {
    std::printf("[diag] host evt=idr: chốt trên thread Encode, in trên vòng Recv...\n");
    SourceDiag s;
    char buf[SourceDiag::kIdrBufBytes];
    Check(s.FormatIdr(buf, sizeof(buf), "Screen 1") == nullptr,
        "evt=idr: chưa chốt gì thì không có dòng nào");

    s.LatchIdr(120'000, 90, 7);
    const char* line = s.FormatIdr(buf, sizeof(buf), "Screen 1");
    Check(line != nullptr, "evt=idr: chốt rồi thì có dòng");
    Check(line && Has(line, "evt=idr bytes=120000 pkts=90 burst_ms=7"), "evt=idr: đủ ba trường");
    Check(s.FormatIdr(buf, sizeof(buf), "Screen 1") == nullptr, "evt=idr: đọc-và-xoá, không in lại");

    // IDR mới đè IDR cũ chưa kịp in — cái gần nhất là cái đáng xem.
    s.LatchIdr(1, 1, 1);
    s.LatchIdr(222, 2, 3);
    line = s.FormatIdr(buf, sizeof(buf), "Screen 1");
    Check(line && Has(line, "bytes=222"), "evt=idr: IDR mới đè IDR cũ");
}

void TestAgentStatus() {
    std::printf("[diag] host: dòng trạng thái, kể cả khi chưa có FEEDBACK...\n");
    char buf[SourceDiag::kStatusBufBytes];
    SourceDiag::Window w;
    w.rate.captureFps = 60.0;
    w.rate.sendFps = 59.0;
    w.rate.sendKbps = 8000.0;
    w.inputApplied = 120;
    w.inputLost = 1;
    w.inputSkipped = 4;

    // Chưa có FEEDBACK nào: phải là "client -", KHÔNG phải "loss 0%, RTT 0 ms".
    SourceDiag::FormatStatus(buf, sizeof(buf), "08:30:00", "Screen 1", "STREAMING", w, {});
    Check(Has(buf, "[Agent t=08:30:00][Screen 1]"), "agent status: tiền tố + giờ + tên nguồn");
    Check(Has(buf, "STREAMING"), "agent status: trạng thái phiên");
    Check(Has(buf, "capture 60 fps"), "agent status: nhịp chụp");
    Check(Has(buf, "send 59 fps, 8000 kbps"), "agent status: nhịp gửi");
    Check(Has(buf, "input 120 (lost 1, skipped 4)"), "agent status: bộ ba số liệu input");
    Check(Has(buf, "| client -"), "agent status: chưa có feedback thì in dấu gạch");

    SourceDiag::LinkView link;
    link.have = true;
    link.lossPct = 3;
    link.rttMs = 12;
    link.recvKbps = 7600;
    SourceDiag::FormatStatus(buf, sizeof(buf), "08:30:01", "Screen 1", "STREAMING", w, link);
    Check(Has(buf, "| client loss 3%, RTT 12 ms, recv 7600 kbps"),
        "agent status: có feedback thì in số liệu đầu kia");
}

void TestAgentLoopSum() {
    std::printf("[diag] host evt=sum: sức khoẻ vòng Recv...\n");
    AgentDiag a;
    char buf[AgentDiag::kSumBufBytes];
    a.loopBusyMs.Add(180);
    a.FormatSum(buf, sizeof(buf), "01:02:03");
    Check(Has(buf, "[DIAG][agent] evt=sum t=01:02:03 loop_busy_ms_max=180"),
        "agent evt=sum: sức khoẻ vòng Recv");
    a.FormatSum(buf, sizeof(buf), "01:02:04");
    Check(Has(buf, "loop_busy_ms_max=0"), "agent evt=sum: đọc-và-xoá");
}

// Buffer chật phải CẮT chứ không tràn. Kiểm tra bằng canary hai đầu.
void TestTruncation() {
    std::printf("[diag] buffer chật thì CẮT, không tràn...\n");
    struct Guarded {
        char pre[8];
        char buf[24];
        char post[8];
    } g;
    std::memset(&g, 0x7E, sizeof(g));

    ClientDiag d;
    d.FormatSum(g.buf, sizeof(g.buf), "01:02:03", MakeWindow(), 0, 0);
    Check(std::strlen(g.buf) < sizeof(g.buf), "cắt: chuỗi luôn kết thúc trong buffer");
    bool intact = true;
    for (char c : g.pre) intact = intact && c == 0x7E;
    for (char c : g.post) intact = intact && c == 0x7E;
    Check(intact, "cắt: không ghi ra ngoài buffer");

    SourceDiag s;
    s.FormatSum(g.buf, sizeof(g.buf), "01:02:03", "Screen 1", 0, false);
    Check(std::strlen(g.buf) < sizeof(g.buf), "cắt: bản host cũng kết thúc trong buffer");
    intact = true;
    for (char c : g.pre) intact = intact && c == 0x7E;
    for (char c : g.post) intact = intact && c == 0x7E;
    Check(intact, "cắt: bản host không ghi ra ngoài buffer");
}

} // namespace

void RunDiagTests() {
    TestWindowStat();
    TestCountMaxMin();
    TestClientSum();
    TestClientStatus();
    TestSourceDiag();
    TestSourceIdr();
    TestAgentStatus();
    TestAgentLoopSum();
    TestTruncation();
}
