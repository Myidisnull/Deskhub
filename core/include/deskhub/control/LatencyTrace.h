#pragma once
// =============================================================================
// LatencyTrace.h — dãy mẫu độ trễ để vẽ biểu đồ đường, chung cho mọi client.
//
// NHIỆM VỤ
//   Overlay và màn hình "Link check" đều có một biểu đồ đường nhỏ: độ trễ của khoảng
//   một phút vừa qua, một cột mỗi mẫu. Lớp này là cái vòng đệm đứng sau nó.
//
// VÌ SAO KHÔNG PHẢI LÀ MỘT MẢNG TRONG UI
//   Vì hai quyết định trong đó là quyết định về DỮ LIỆU, không phải về hình vẽ, và
//   sáu client phải trả lời giống nhau:
//
//   1. NHỊP LẤY MẪU. PONG về mỗi giây, nhưng biểu đồ 60 cột lấy mẫu mỗi giây thì
//      trải ra một phút — đủ chậm để một lần giật 200 ms trôi qua mà không kịp thành
//      một cột nào. Lấy mẫu mỗi 320 ms cho khoảng 19 giây trên 60 cột: đủ dày để
//      thấy được cái giật, đủ dài để thấy được xu hướng. Đây cũng chính là nhịp
//      thiết kế giao diện ghi trên nhãn biểu đồ ("sample every 320 ms").
//
//   2. GIỮ MẪU CAO NHẤT GIỮA HAI LẦN LẤY. Giữa hai mốc lấy mẫu có thể có nhiều số
//      đo. Lấy số CUỐI thì một lần giật rơi đúng vào giữa hai mốc sẽ biến mất khỏi
//      biểu đồ — mà đó lại chính là thứ người dùng đang tìm khi họ mở biểu đồ ra.
//      Nên giữ số LỚN NHẤT: biểu đồ này để phát hiện chỗ tệ, không phải để báo cáo
//      giá trị trung bình (đã có StatsLine lo).
//
// ĐƠN VỊ
//   Mili-giây, u16. Trễ > 65 giây thì phiên đã chết từ lâu rồi.
//
// LIÊN QUAN: deskhub/control/ClockSync.h (nguồn số e2e),
//            deskhub/control/LinkStats.h (số liệu theo cửa sổ 1 giây)
// =============================================================================
#include <cstddef>
#include <cstdint>
#include <span>

namespace deskhub {

// Số cột của biểu đồ. Khớp với dãy mẫu trong thiết kế giao diện.
inline constexpr size_t kLatencyTraceLen = 60;
inline constexpr uint64_t kLatencySampleUs = 320'000;

class LatencyTrace {
public:
    explicit LatencyTrace(uint64_t sampleUs = kLatencySampleUs) : sampleUs_(sampleUs) {}

    // Một số đo mới (RTT hoặc e2e — một trace cho một đại lượng). Tự gom vào mốc lấy
    // mẫu hiện tại và chốt cột khi hết mốc; gọi bao nhiêu lần một giây cũng được.
    void Add(uint16_t ms, uint64_t nowUs);

    // Chép các mẫu theo thứ tự CŨ → MỚI vào `out`, trả số mẫu đã ghi (≤ out.size()).
    // Cũ → mới vì biểu đồ đường vẽ trái sang phải theo thời gian; trả về đúng thứ tự
    // vẽ để phía giao diện không phải đảo lại (sáu lần, mỗi nơi một kiểu).
    size_t Snapshot(std::span<uint16_t> out) const;

    size_t size() const {
        return count_;
    }
    bool empty() const {
        return count_ == 0;
    }

    // Số liệu tóm tắt của đúng dãy mẫu đang hiện — để nhãn dưới biểu đồ khớp với hình
    // vẽ. Tính lại mỗi lần gọi: dãy chỉ có 60 phần tử, giữ tổng tích luỹ song song
    // với một vòng đệm ghi đè là cách hay sai nhất.
    uint16_t Min() const;
    uint16_t Max() const;
    uint16_t Last() const;
    double Avg() const;

    void Clear();

private:
    uint16_t samples_[kLatencyTraceLen] = {}; // vòng đệm
    size_t head_ = 0;                         // vị trí ghi kế tiếp
    size_t count_ = 0;                        // số mẫu đang giữ, ≤ kLatencyTraceLen
    uint16_t pending_ = 0;                    // mẫu lớn nhất thấy trong mốc đang chạy
    bool havePending_ = false;
    uint64_t markUs_ = 0; // đầu mốc lấy mẫu đang chạy
    bool haveMark_ = false;
    uint64_t sampleUs_;
};

} // namespace deskhub
