// =============================================================================
// ClockOffsetTests.cpp — bộ lọc min ước lượng độ trễ một chiều (ClockOffset).
//
// Điều PHẢI kiểm chứng được ở đây, vì nó là toàn bộ lý do lớp này tồn tại: con số
// trả ra KHÔNG phụ thuộc vào độ lệch đồng hồ giữa hai máy. Nên mọi ca dưới đây chạy
// hai lần với hai giá trị C hoàn toàn khác nhau (máy host vừa khởi động vs. đã chạy
// ba ngày) và đòi kết quả GIỐNG HỆT.
// =============================================================================
#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/control/ClockOffset.h"

#include <cstdio>

using namespace deskhub;

namespace {

// Hai gốc đồng hồ dùng cho mọi ca: một máy vừa bật, một máy chạy ba ngày. Chênh
// lệch âm khổng lồ là ca mà uint64_t sẽ tràn — chính là ca phải chạy đúng.
constexpr int64_t kSkewFresh = 0;
constexpr int64_t kSkewStale = -3ll * 24 * 3600 * 1'000'000; // client "trẻ" hơn host 3 ngày

// Bơm một frame: host chụp lúc `hostUs`, tới đích sau `delayUs`.
void Feed(ClockOffset& co, int64_t skew, uint64_t hostUs, uint64_t delayUs) {
    co.AddSample(hostUs, uint64_t(int64_t(hostUs) + int64_t(delayUs) + skew));
}

void TestFloorIsSubtracted(int64_t skew) {
    ClockOffset co;
    // Sàn thật của đường này là 20 ms; frame thứ ba gặp một cú xếp hàng 50 ms.
    Feed(co, skew, 1'000'000, 20'000);
    Check(co.LatencyUs() == 0, "frame đầu tiên định nghĩa sàn -> 0");
    Feed(co, skew, 1'016'000, 22'000);
    Check(co.LatencyUs() == 2'000, "frame nhỉnh hơn sàn 2 ms");
    Feed(co, skew, 1'032'000, 70'000);
    Check(co.LatencyUs() == 50'000, "cú xếp hàng 50 ms lộ ra nguyên vẹn");
    // Sàn cũ vẫn giữ: một frame nhanh trở lại phải về 0, không phải về "50 ms mới".
    Feed(co, skew, 1'048'000, 20'000);
    Check(co.LatencyUs() == 0, "trở lại mức tốt nhất -> 0");
}

void TestNetFloorAddedBack(int64_t skew) {
    ClockOffset co;
    Feed(co, skew, 1'000'000, 20'000);
    Feed(co, skew, 1'016'000, 45'000);
    Check(co.LatencyUs() == 25'000, "không cộng sàn: chỉ phần vượt");
    Check(co.LatencyUs(4'000) == 29'000, "cộng lại sàn mạng đo được (minRtt/2)");
    Check(co.LatencyUs(0) == 25'000, "sàn 0 = y như không truyền");
}

// Đường truyền xuống cấp giữa phiên (roam Wi-Fi): sàn thật nhảy từ 20 ms lên 120 ms.
// Bộ lọc phải QUÊN sàn cũ, nếu không mọi frame sau đó bị báo trễ +100 ms vĩnh viễn.
void TestFloorDecays(int64_t skew) {
    ClockOffset co;
    uint64_t t = 1'000'000;
    Feed(co, skew, t, 20'000);

    // Hai cửa sổ đầy ở mức mới. Sau khi cả xô hiện tại lẫn xô trước đều chỉ chứa
    // mẫu "chậm", sàn cũ không còn chỗ nào để nấp.
    for (int i = 0; i < 40; ++i) {
        t += ClockOffset::kWindowUs / 10;
        Feed(co, skew, t, 120'000);
    }
    Check(co.LatencyUs() == 0, "sàn đã học lại mức mới -> frame ở mức đó là 0");

    // Và mức CŨ giờ trông như đến sớm — vẫn kẹp ở 0, không bao giờ âm.
    t += ClockOffset::kWindowUs / 10;
    Feed(co, skew, t, 20'000);
    Check(co.LatencyUs() == 0, "nhanh hơn sàn hiện tại vẫn kẹp ở 0, không âm");
}

// Nguồn tĩnh: host chỉ phát keepalive, im lặng dài hơn NHIỀU cửa sổ. Rotate phải
// nhảy qua hết các cửa sổ rỗng, không thì mẫu sau đó bị so với sàn của một phút trước.
void TestLongSilence(int64_t skew) {
    ClockOffset co;
    Feed(co, skew, 1'000'000, 20'000);
    // Im lặng 6 cửa sổ, rồi một frame ở đúng mức cũ.
    const uint64_t t = 1'000'000 + ClockOffset::kWindowUs * 6;
    Feed(co, skew, t, 20'000);
    Check(co.LatencyUs() == 0, "sau im lặng dài, mẫu đầu tiên định nghĩa lại sàn");
    Feed(co, skew, t + 16'000, 35'000);
    Check(co.LatencyUs() == 15'000, "và mẫu kế đo đúng phần vượt");
}

void TestNotReady() {
    ClockOffset co;
    Check(!co.ready(), "chưa có mẫu nào");
    Check(co.LatencyUs() == -1, "chưa sẵn sàng -> -1, không phải 0");
    co.AddSample(1'000'000, 1'020'000);
    Check(co.ready(), "có mẫu là sẵn sàng");
    co.Reset();
    Check(!co.ready(), "Reset quên sạch (phiên mới mang một C khác)");
}

// Chạy một ca với cả hai gốc đồng hồ và đòi kết quả không đổi.
void BothSkews(void (*fn)(int64_t), const char* name) {
    std::printf("[clock] %s...\n", name);
    fn(kSkewFresh);
    fn(kSkewStale);
}

} // namespace

void RunClockOffsetTests() {
    BothSkews(TestFloorIsSubtracted, "sàn bị trừ đi, phần xếp hàng lộ ra");
    BothSkews(TestNetFloorAddedBack, "sàn mạng đo được cộng lại, không tính hai lần");
    BothSkews(TestFloorDecays, "sàn học lại khi đường truyền xuống cấp");
    BothSkews(TestLongSilence, "nguồn tĩnh im lặng nhiều cửa sổ");
    std::printf("[clock] chưa có mẫu / Reset...\n");
    TestNotReady();
}
