// =============================================================================
// QualityLadderTests.cpp — thang chất lượng: fps + độ phân giải chọn theo băng thông.
//
// Các mức bitrate dùng ở đây là mức THẬT của những đường truyền hay gặp (Wi-Fi tốt,
// Wi-Fi đông người, 4G, Tailscale qua relay), không phải số tròn bịa ra — đây là chỗ
// duy nhất kiểm chứng được rằng thang thực sự đưa ra bậc dùng được ở từng mức đó.
// =============================================================================
#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/control/QualityLadder.h"

#include <cstdio>

using namespace deskhub;

namespace {

// Trần điển hình: MacBook Retina co về 1920 (xem AgentOptions::maxDim), 60fps.
constexpr uint16_t kW = 1920, kH = 1246;
constexpr uint8_t kFps = 60;

// Bơm cùng một bitrate suốt `seconds` giây, mỗi giây một lần như Feedback thật.
// Trả về mốc thời gian cuối.
uint64_t Hold(QualityLadder& q, uint32_t bps, int seconds, uint64_t t0) {
    uint64_t t = t0;
    for (int i = 0; i < seconds; ++i) {
        t += 1'000'000;
        q.Update(bps, t);
    }
    return t;
}

void TestTopRungOnAHealthyLink() {
    std::printf("[quality] đường truyền tốt -> đúng trần người dùng đặt...\n");
    QualityLadder q(kW, kH, kFps);
    Check(q.current() == QualityStep{kW, kH, kFps}, "khởi tạo ở bậc trần");
    // 20 Mbps là mặc định của agent. Nó phải thừa cho bậc cao nhất, nếu không thì
    // mặc định của sản phẩm đang nằm dưới thang của chính nó.
    const uint32_t need = q.requiredBps();
    Check(need < 20'000'000u, "bậc trần cần dưới 20 Mbps (mức mặc định của agent)");
    Hold(q, 20'000'000, 30, 0);
    Check(q.current() == QualityStep{kW, kH, kFps}, "20 Mbps: không đổi gì cả");
}

void TestFpsGoesFirst() {
    std::printf("[quality] hạ fps TRƯỚC, giữ nguyên pixel...\n");
    QualityLadder q(kW, kH, kFps);
    // Vừa đủ dưới bậc trần -> phải rơi xuống bậc fps thấp hơn mà KHÔNG co pixel.
    q.Update(q.requiredBps() - 1, 1'000'000);
    const QualityStep s = q.current();
    Check(s.width == kW && s.height == kH, "độ phân giải giữ nguyên");
    Check(s.fps < kFps, "fps đã hạ");
}

void TestResolutionOnlyAfterFpsFloor() {
    std::printf("[quality] chỉ co pixel SAU khi fps chạm sàn...\n");
    QualityLadder q(kW, kH, kFps);
    // Đường truyền tệ dần. Ghi lại bậc đầu tiên có co pixel và kiểm rằng lúc đó fps
    // đã ở mức thấp — tức là thang không co pixel sớm.
    uint8_t fpsWhenShrunk = 0;
    for (uint32_t bps = 12'000'000; bps >= 500'000; bps -= 250'000) {
        q.Update(bps, 1'000'000);
        if (q.current().width < kW) {
            fpsWhenShrunk = q.current().fps;
            break;
        }
    }
    Check(fpsWhenShrunk != 0, "có tồn tại bậc co pixel");
    Check(fpsWhenShrunk <= 20, "khi bắt đầu co pixel thì fps đã ở sàn 20");
}

void TestNeverExceedsCeiling() {
    std::printf("[quality] không bao giờ vượt trần, kể cả khi thừa băng thông...\n");
    QualityLadder q(kW, kH, kFps);
    Hold(q, 500'000'000, 120, 0); // 500 Mbps
    Check(q.current() == QualityStep{kW, kH, kFps}, "vẫn đúng trần, không tự phóng lên");
}

void TestDownIsImmediate() {
    std::printf("[quality] tụt là tụt ngay, một lần Update...\n");
    QualityLadder q(kW, kH, kFps);
    // Đường truyền sập xuống 1.5 Mbps (Wi-Fi đông người). Không có dwell nào ở chiều
    // này: hàng đợi đang đầy, chần chừ một giây là thêm một giây gói bị vứt.
    Check(q.Update(1'500'000, 1'000'000), "một lần Update là đã tụt");
    Check(q.rung() > 0, "đã rời bậc trần");
    Check(q.requiredBps() <= 1'500'000, "bậc mới nằm trong khả năng của đường truyền");

    // Và tụt tiếp thì cũng ngay lập tức, không bị dwell của chiều lên chặn.
    const int mid = q.rung();
    Check(q.Update(400'000, 2'000'000), "tụt tiếp cũng tức thì");
    Check(q.rung() > mid, "xuống bậc thấp hơn nữa");
}

void TestUpNeedsHeadroom() {
    std::printf("[quality] lên bậc đòi dư 20%%, vừa đủ thì không lên...\n");
    QualityLadder q(kW, kH, kFps);
    q.Update(1'500'000, 1'000'000);
    const int low = q.rung();

    // Đúng bằng mức bậc hiện tại cần — không dư gram nào. Giữ suốt 60 giây vẫn
    // không được lên: dò lên khi chỉ vừa đủ chính là cách tạo lại cái nghẽn vừa thoát.
    Hold(q, q.requiredBps(), 60, 1'000'000);
    Check(q.rung() == low, "vừa đủ, không dư -> ở nguyên bậc");
}

// Lên bậc mà KHÔNG đổi độ phân giải (chỉ fps) là thay đổi rẻ -> chờ kUpDwellUs (5s).
void TestFpsOnlyStepUpIsQuick() {
    std::printf("[quality] lên bậc chỉ-đổi-fps: chờ 5 giây...\n");
    QualityLadder q(kW, kH, kFps);
    // 4 Mbps rơi vào bậc 1920x1246@20 — bậc trên nó cũng 1920x1246, chỉ khác fps.
    q.Update(4'000'000, 1'000'000);
    const QualityStep low = q.current();
    Check(low.width == kW, "vẫn full pixel, chỉ hạ fps");

    uint64_t t = Hold(q, 500'000'000, 4, 1'000'000); // t = 5s
    Check(q.current() == low, "4 giây: chưa đủ dwell");
    t = Hold(q, 500'000'000, 3, t); // t = 8s
    Check(q.current().fps > low.fps, "qua 5 giây: fps lên lại");
    Check(q.current().width == kW, "và pixel vẫn nguyên");
}

// Lên bậc CÓ đổi độ phân giải bắt dựng lại encoder + IDR + client dựng lại decoder
// -> chờ kUpDwellResizeUs (15s). Nhấp nháy độ phân giải hại hơn ở lì bậc thấp.
void TestResizeStepUpWaitsLonger() {
    std::printf("[quality] lên bậc đổi-độ-phân-giải: chờ 15 giây...\n");
    QualityLadder q(kW, kH, kFps);
    q.Update(1'500'000, 1'000'000); // bậc đã co pixel
    const QualityStep low = q.current();
    Check(low.width < kW, "đang ở bậc đã co pixel");

    uint64_t t = Hold(q, 500'000'000, 10, 1'000'000); // t = 11s
    Check(q.current() == low, "10 giây: chưa đủ — bậc kế đòi đổi độ phân giải");
    t = Hold(q, 500'000'000, 8, t); // t = 19s
    Check(q.current().width > low.width, "qua 15 giây: độ phân giải lên lại");
}

void TestUpOneRungAtATime() {
    std::printf("[quality] lên thì mỗi lần một bậc, không nhảy cóc...\n");
    QualityLadder q(kW, kH, kFps);
    q.Update(400'000, 1'000'000); // đáy thang
    int prev = q.rung();
    uint64_t t = 1'000'000;
    // Băng thông vô hạn suốt 2 phút. Mỗi lần bậc đổi, nó chỉ được đổi đúng 1.
    for (int i = 0; i < 120; ++i) {
        t += 1'000'000;
        if (q.Update(500'000'000, t)) {
            Check(q.rung() == prev - 1, "mỗi lần lên đúng một bậc");
            prev = q.rung();
        }
    }
    Check(q.rung() == 0, "cuối cùng vẫn về được bậc trần");
}

void TestLowUserFpsCollapsesDuplicateRungs() {
    std::printf("[quality] trần fps thấp làm các bậc trùng bị gộp...\n");
    QualityLadder q60(kW, kH, 60);
    QualityLadder q30(kW, kH, 30);
    Check(q30.rungCount() < q60.rungCount(), "trần 30fps -> thang ngắn hơn");
    Check(q30.current() == QualityStep{kW, kH, 30}, "bậc trần tôn trọng trần người dùng");
    // Và mọi bậc vẫn phải nằm dưới trần đó.
    for (uint32_t bps = 20'000'000; bps >= 300'000; bps -= 500'000) {
        q30.Update(bps, 1'000'000);
        Check(q30.current().fps <= 30, "không bậc nào vượt trần fps");
    }
}

void TestTinySourceStopsTheLadder() {
    std::printf("[quality] nguồn bé thì thang không co xuống dưới mức nén được...\n");
    // Một cửa sổ nhỏ: co 50% là chạm mức bộ nén từ chối.
    QualityLadder q(320, 200, 60);
    for (uint32_t bps = 20'000'000; bps >= 100'000; bps -= 200'000) {
        q.Update(bps, 1'000'000);
        Check(q.current().width >= 160 && q.current().height >= 64,
            "mọi bậc vẫn nén được");
    }
}

} // namespace

void RunQualityLadderTests() {
    TestTopRungOnAHealthyLink();
    TestFpsGoesFirst();
    TestResolutionOnlyAfterFpsFloor();
    TestNeverExceedsCeiling();
    TestDownIsImmediate();
    TestUpNeedsHeadroom();
    TestFpsOnlyStepUpIsQuick();
    TestResizeStepUpWaitsLonger();
    TestUpOneRungAtATime();
    TestLowUserFpsCollapsesDuplicateRungs();
    TestTinySourceStopsTheLadder();
}
