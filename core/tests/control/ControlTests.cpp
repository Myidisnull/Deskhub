// =============================================================================
// ControlTests.cpp — chính sách điều tiết: BitrateController (tụt nhanh/lên chậm,
// bật-tắt FEC) và LinkStats (delta theo cửa sổ, dựng Feedback).
// =============================================================================
#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/control/BitrateController.h"
#include "deskhub/control/ClockSync.h"
#include "deskhub/control/LatencyTrace.h"
#include "deskhub/control/LinkStats.h"

#include <cstdio>

using namespace deskhub;

namespace {

Feedback Fb(uint8_t lossPct, uint16_t rttMs = 20) {
    Feedback fb{};
    fb.lossPct = lossPct;
    fb.rttMs = rttMs;
    return fb;
}

void TestBitrateBackoff() {
    std::printf("[ctrl] bitrate: back off on loss, clamp at floor...\n");
    BitrateController c(20'000'000, 1'000'000);

    auto d = c.Update(Fb(5), 1'000'000);
    Check(d.changeBitrate && d.bitrateBps == 15'000'000, "loss 5% -> x0.75");
    c.CommitBitrate(d.bitrateBps);

    d = c.Update(Fb(2), 2'000'000);
    Check(d.changeBitrate && d.bitrateBps == 13'500'000, "loss 2% -> x0.90");
    c.CommitBitrate(d.bitrateBps);

    uint64_t now = 3'000'000;
    for (int i = 0; i < 40; ++i, now += 1'000'000) {
        d = c.Update(Fb(9), now);
        if (d.changeBitrate) c.CommitBitrate(d.bitrateBps);
    }
    Check(c.bitrateBps() >= 1'000'000, "sustained loss never drops below the floor");
    Check(c.bitrateBps() < 1'020'000, "sustained loss settles at the floor (within the 2% deadband)");

    const auto stable = c.Update(Fb(9), now);
    Check(!stable.changeBitrate, "at the floor -> no further renegotiation");
}

void TestBitrateRecovery() {
    std::printf("[ctrl] bitrate: ramp back up only after the link stays clean...\n");
    BitrateController c(20'000'000, 1'000'000);

    auto d = c.Update(Fb(5), 1'000'000);
    c.CommitBitrate(d.bitrateBps); // 15 Mbps, lastDecrease = 1s

    d = c.Update(Fb(0), 2'500'000);
    Check(!d.changeBitrate, "no ramp-up within the 2s cooldown after a decrease");

    d = c.Update(Fb(0), 4'000'000);
    Check(d.changeBitrate && d.bitrateBps == 16'000'000, "ramp-up is +5% of the ceiling");
    c.CommitBitrate(d.bitrateBps);

    uint64_t now = 5'000'000;
    for (int i = 0; i < 20; ++i, now += 1'000'000) {
        d = c.Update(Fb(0), now);
        if (d.changeBitrate) c.CommitBitrate(d.bitrateBps);
    }
    Check(c.bitrateBps() == 20'000'000, "ramp-up stops at the ceiling");
    d = c.Update(Fb(0), now);
    Check(!d.changeBitrate, "already at ceiling -> no renegotiation");
}

void TestBitrateUncommitted() {
    std::printf("[ctrl] bitrate: rejected change doesn't move the controller...\n");
    BitrateController c(20'000'000, 1'000'000);
    const auto d = c.Update(Fb(5), 1'000'000);
    Check(d.changeBitrate && d.bitrateBps == 15'000'000, "proposes the decrease");
    Check(c.bitrateBps() == 20'000'000, "but stays put until CommitBitrate");
    const auto d2 = c.Update(Fb(5), 2'000'000);
    Check(d2.bitrateBps == 15'000'000, "next round recomputes from the old rate");
}

void TestFecHysteresis() {
    std::printf("[ctrl] FEC: on immediately, off only after 5 clean seconds...\n");
    BitrateController c(20'000'000, 1'000'000);

    auto d = c.Update(Fb(0), 1'000'000);
    Check(!d.fecEnabled && !d.fecToggled, "FEC starts off and stays off on a clean link");

    d = c.Update(Fb(3), 2'000'000);
    Check(d.fecEnabled && d.fecToggled, "any real loss turns FEC on at once");

    uint64_t now = 3'000'000;
    for (int i = 0; i < 4; ++i, now += 1'000'000) {
        d = c.Update(Fb(0), now);
        Check(d.fecEnabled && !d.fecToggled, "FEC stays on through 4 clean seconds");
    }
    d = c.Update(Fb(0), now);
    Check(!d.fecEnabled && d.fecToggled, "FEC turns off on the 5th clean second");

    c.Update(Fb(4), now + 1'000'000);
    now += 2'000'000;
    for (int i = 0; i < 4; ++i, now += 1'000'000) c.Update(Fb(0), now);
    Check(c.fecEnabled(), "clean-second counter restarts after a fresh loss");
}

void TestLinkStatsWindow() {
    std::printf("[ctrl] LinkStats: per-window deltas and rates...\n");
    LinkStats ls(0);
    Check(!ls.Due(999'999), "window not due before 1s");
    Check(ls.Due(1'000'000), "window due at 1s");

    Reassembler::Stats st{};
    st.packetsReceived = 900;
    st.packetsLost = 100;
    st.packetsRecovered = 7;
    st.framesDropped = 3;
    st.lossRuns[0] = 2;
    st.lossRuns[3] = 1;
    st.lossRunMax = 6;

    const LinkWindow w = ls.Close(st, 250'000 /*bytes*/, 60 /*frames*/, 1'000'000);
    Check(w.packetsLost == 100 && w.packetsReceived == 900, "first window = raw counters");
    Check(w.lossPct > 9.99 && w.lossPct < 10.01, "lossPct = lost/(lost+received)");
    Check(w.fps > 59.9 && w.fps < 60.1, "fps from rendered count");
    Check(w.kbps > 1999.0 && w.kbps < 2001.0, "kbps from video bytes");
    Check(w.lossRunTotal == 3 && w.lossRunMax == 6, "loss-run buckets summed, max passed through");

    st.packetsReceived += 1000;
    st.framesDropped += 1;
    const LinkWindow w2 = ls.Close(st, 500'000, 60, 2'000'000);
    Check(w2.packetsReceived == 1000 && w2.packetsLost == 0, "second window is a delta");
    Check(w2.lossPct == 0.0, "clean second reports 0% loss");
    Check(w2.framesDropped == 1, "dropped frames are per-window too");

    // Gói về muộn: đếm theo cửa sổ, độ muộn trung bình tính trên đúng cửa sổ này,
    // lateMsMax là kỷ lục tích luỹ chép thẳng qua.
    st.latePackets += 4;
    st.lateMsSum += 100;
    st.lateMsMax = 60;
    const LinkWindow w3 = ls.Close(st, 0, 0, 3'000'000);
    Check(w3.latePackets == 4 && w3.lateMsAvg == 25.0 && w3.lateMsMax == 60,
        "late-packet stats are per-window, avg over this window only");
}

void TestLinkStatsUsesRealElapsed() {
    std::printf("[ctrl] LinkStats: rates use the real window length...\n");
    LinkStats ls(0);
    Reassembler::Stats st{};
    const LinkWindow w = ls.Close(st, 250'000, 60, 2'000'000); // cửa sổ dài 2s
    Check(w.secs > 1.99 && w.secs < 2.01, "window length is measured, not assumed");
    Check(w.fps > 29.9 && w.fps < 30.1, "60 frames over 2s = 30 fps, not 60");
}

void TestFeedbackFromWindow() {
    std::printf("[ctrl] LinkStats: Feedback packet mirrors the window...\n");
    LinkWindow w;
    w.lossPct = 3.6;
    w.framesDropped = 2;
    w.kbps = 8'500.4;

    const Feedback fb = MakeFeedback(w, 21'400 /*rttUs*/);
    Check(fb.lossPct == 4, "lossPct rounds to nearest (3.6 -> 4)");
    Check(fb.lostFrames == 2, "lostFrames carried through");
    Check(fb.rttMs == 21, "RTT converted us -> ms");
    Check(fb.recvBitrateKbps == 8500, "recv bitrate carried through");

    LinkWindow clean;
    const Feedback fb2 = MakeFeedback(clean, 0);
    Check(fb2.lossPct == 0 && fb2.lostFrames == 0, "a clean window is still a valid Feedback");
}

void TestLinkStatsE2e() {
    std::printf("[ctrl] LinkStats: end-to-end latency is per-window too...\n");
    LinkStats ls(0);
    Reassembler::Stats st{};

    const LinkWindow quiet = ls.Close(st, 0, 0, 1'000'000);
    Check(quiet.e2eSamples == 0 && quiet.e2eMsAvg == 0.0,
        "a second with nothing displayed reports 0 samples, not 0 ms");

    ls.AddE2e(10'000);
    ls.AddE2e(12'000);
    ls.AddE2e(200'000); // một frame giật
    const LinkWindow w = ls.Close(st, 0, 3, 2'000'000);
    Check(w.e2eSamples == 3, "one sample per displayed frame");
    Check(w.e2eMsAvg > 73.9 && w.e2eMsAvg < 74.1, "avg over the window, in ms");
    Check(w.e2eMsMax == 200, "max keeps the stutter the average hides");

    const LinkWindow w2 = ls.Close(st, 0, 0, 3'000'000);
    Check(w2.e2eSamples == 0 && w2.e2eMsMax == 0, "the accumulator resets each window");
}

void TestClockSyncOffset() {
    std::printf("[ctrl] ClockSync: cancels the clock offset, keeps the latency...\n");
    // Đồng hồ host đi trước client đúng 1 giờ — con số này phải biến mất hoàn toàn,
    // nếu không overlay sẽ hiện "e2e 3600000 ms".
    constexpr int64_t kSkew = 3'600'000'000; // 1 giờ tính bằng us
    ClockSync cs;
    cs.OnRtt(8'000); // RTT 8 ms → một chiều ước lượng 4 ms

    // Mười frame, mỗi frame mất 20 ms đường truyền (một chiều thật), trừ frame thứ 5
    // đi nhanh nhất với 6 ms — đó là frame nuôi bộ lọc cực tiểu.
    uint64_t hostTs = 1'000'000;
    for (int i = 0; i < 10; ++i, hostTs += 16'666) {
        const uint64_t oneWay = (i == 5) ? 6'000 : 20'000;
        cs.OnFrame(hostTs, uint64_t(int64_t(hostTs) + kSkew + int64_t(oneWay)));
    }
    Check(cs.ready(), "ready after the first frame");

    // Frame tiếp theo: đường truyền 20 ms, giải mã + hiển thị thêm 5 ms.
    const uint64_t ts = hostTs;
    const uint64_t shown = uint64_t(int64_t(ts) + kSkew + 20'000 + 5'000);
    const uint32_t e2e = cs.E2eUs(ts, shown);
    // Đo được = (20+5) − 6 (cực tiểu bị trừ) + 4 (nửa RTT cộng lại) = 23 ms.
    Check(e2e > 22'000 && e2e < 24'000, "offset cancelled, real latency survives");

    // Frame nhanh nhất KHÔNG được báo 0: nửa RTT là sàn ước lượng.
    const uint32_t best = cs.E2eUs(ts, uint64_t(int64_t(ts) + kSkew + 6'000));
    Check(best >= 3'000 && best <= 5'000, "the fastest frame reports ~rtt/2, not 0");
}

void TestClockSyncSeedAndClamp() {
    std::printf("[ctrl] ClockSync: HELLO_ACK seeds it, negatives clamp to 0...\n");
    ClockSync cs;
    Check(!cs.ready(), "nothing to report before any input");
    Check(cs.E2eUs(1'000, 2'000) == 0, "not ready -> 0");

    // Rebase để những frame đầu phiên đã có số hiện, thay vì ô trống vài giây.
    cs.Rebase(500'000 /*host*/, 900'000 /*local*/);
    Check(cs.ready(), "HELLO_ACK is enough to start reporting");
    Check(cs.E2eUs(600'000, 1'020'000) > 0, "a later frame reads as positive latency");

    // Frame đi nhanh hơn cả gốc hiện tại → hiệu âm. "Trễ âm" vô nghĩa hơn là 0.
    Check(cs.E2eUs(600'000, 900'000) == 0, "a negative estimate clamps to 0");
}

void TestLatencyTrace() {
    std::printf("[ctrl] LatencyTrace: 320ms buckets, keeps the worst of each...\n");
    LatencyTrace t(320'000);
    Check(t.empty(), "starts empty");

    uint64_t now = 0;
    // Trong cùng một mốc: 4, 40, 5 → cột phải là 40. Lấy mẫu cuối sẽ nuốt mất cái giật.
    t.Add(4, now);
    t.Add(40, now + 100'000);
    t.Add(5, now + 200'000);
    Check(t.empty(), "nothing committed before the bucket closes");
    t.Add(6, now + 330'000);
    Check(t.size() == 1, "bucket closes at 320ms");
    Check(t.Last() == 40, "the bucket keeps its worst sample, not its last");

    // Đổ đầy quá sức chứa: vòng đệm giữ các mẫu MỚI NHẤT, đúng thứ tự.
    LatencyTrace f(320'000);
    now = 0;
    for (uint16_t i = 1; i <= 80; ++i) {
        now += 320'000;
        f.Add(i, now);
    }
    Check(f.size() == kLatencyTraceLen, "capped at the trace length");
    uint16_t buf[kLatencyTraceLen] = {};
    const size_t n = f.Snapshot(buf);
    Check(n == kLatencyTraceLen, "snapshot fills the buffer");
    // Cột đầu là mẫu CŨ NHẤT còn giữ. Ghi nhầm chỗ này thì biểu đồ vẫn trông hợp lý,
    // chỉ là các cột bị xoay vòng.
    Check(buf[0] < buf[n - 1], "oldest first, newest last");
    Check(buf[n - 1] == 79, "the newest committed bucket is last");
    Check(buf[0] == 20, "the oldest surviving bucket is first");

    Check(f.Max() == 79 && f.Min() == 20, "min/max match the samples on screen");
    Check(f.Avg() > 49.0 && f.Avg() < 50.0, "avg over the visible samples");

    // Cửa sổ nhỏ hơn dãy: lấy các mẫu mới nhất, vẫn cũ → mới.
    uint16_t small[5] = {};
    Check(f.Snapshot(small) == 5, "a smaller view takes the newest samples");
    Check(small[0] == 75 && small[4] == 79, "...still oldest-to-newest");

    f.Clear();
    Check(f.empty() && f.Last() == 0, "Clear resets");
}

void TestLatencyTraceGaps() {
    std::printf("[ctrl] LatencyTrace: a long stall is a flat line, not a zero...\n");
    LatencyTrace t(320'000);
    t.Add(30, 0);
    t.Add(30, 400'000); // chốt cột đầu
    Check(t.size() == 1, "one bucket so far");

    // Máy ngủ 3 giây → nhảy qua ~9 mốc. Vẽ 0 sẽ là nói dối (0 ms = đường cực tốt).
    t.Add(35, 3'400'000);
    Check(t.size() > 5, "the skipped buckets are filled in, not collapsed into one");
    uint16_t buf[kLatencyTraceLen] = {};
    const size_t n = t.Snapshot(buf);
    bool anyZero = false;
    for (size_t i = 0; i < n; ++i)
        if (buf[i] == 0) anyZero = true;
    Check(!anyZero, "a stall never draws as 0 ms");
}

} // namespace

void RunControlTests() {
    TestBitrateBackoff();
    TestBitrateRecovery();
    TestBitrateUncommitted();
    TestFecHysteresis();
    TestLinkStatsWindow();
    TestLinkStatsUsesRealElapsed();
    TestFeedbackFromWindow();
    TestLinkStatsE2e();
    TestClockSyncOffset();
    TestClockSyncSeedAndClamp();
    TestLatencyTrace();
    TestLatencyTraceGaps();
}
