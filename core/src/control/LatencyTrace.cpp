// =============================================================================
// LatencyTrace.cpp — cài đặt vòng đệm mẫu độ trễ.
//
// Điểm dễ sai duy nhất trong file này là chỉ số của Snapshot: vòng đệm ghi đè nên
// mẫu CŨ NHẤT không nằm ở đầu mảng mà ở ngay SAU con trỏ ghi (khi đã đầy). Viết
// nhầm chỗ đó thì biểu đồ vẫn vẽ ra một đường trông hợp lý — chỉ là các cột bị xoay
// vòng, và không ai phát hiện ra cho tới khi so với log.
//
// LIÊN QUAN: deskhub/control/LatencyTrace.h (nhịp lấy mẫu và vì sao giữ mẫu cao nhất)
// =============================================================================
#include "deskhub/control/LatencyTrace.h"

namespace deskhub {

void LatencyTrace::Add(uint16_t ms, uint64_t nowUs) {
    if (!haveMark_) {
        markUs_ = nowUs;
        haveMark_ = true;
    }

    // CHỐT TRƯỚC, GOM SAU. Mẫu vừa tới thuộc về mốc CHỨA nowUs, nên các mốc đã hết
    // hạn phải được chốt bằng những gì gom được TRƯỚC nó. Gom trước rồi mới chốt sẽ
    // dồn mẫu của mốc mới vào cột của mốc cũ — sai lệch một cột, thấy được ngay khi
    // so biểu đồ với log.
    //
    // Dùng while chứ không if: một khoảng ngừng dài (máy ngủ, cửa sổ thu nhỏ) nhảy
    // qua nhiều mốc cùng lúc, và if sẽ dồn tất cả vào một cột duy nhất.
    while (nowUs - markUs_ >= sampleUs_) {
        // Mốc không có số đo nào (khoảng ngừng): lặp lại cột vừa chốt để đường liền
        // mạch. Vẽ 0 thì trung thực hơn về "không có dữ liệu" nhưng lại nói dối về
        // chất lượng — 0 ms là đường hoàn hảo, đúng ngược với cái vừa xảy ra.
        const uint16_t v = havePending_ ? pending_ : (count_ ? Last() : ms);
        samples_[head_] = v;
        head_ = (head_ + 1) % kLatencyTraceLen;
        if (count_ < kLatencyTraceLen) ++count_;
        markUs_ += sampleUs_;
        havePending_ = false;
    }

    // Giữ mẫu LỚN NHẤT của mốc đang chạy — biểu đồ này để tìm chỗ giật (xem header).
    if (!havePending_ || ms > pending_) {
        pending_ = ms;
        havePending_ = true;
    }
}

size_t LatencyTrace::Snapshot(std::span<uint16_t> out) const {
    const size_t n = count_ < out.size() ? count_ : out.size();
    if (!n) return 0;
    // Mẫu cũ nhất nằm ngay sau con trỏ ghi khi đệm đã đầy; khi chưa đầy thì head_
    // chính là số phần tử, nên công thức dưới đây đúng cho cả hai trường hợp. Lấy
    // `n` mẫu MỚI NHẤT khi `out` nhỏ hơn dãy đang giữ.
    const size_t first = (head_ + kLatencyTraceLen - n) % kLatencyTraceLen;
    for (size_t i = 0; i < n; ++i) out[i] = samples_[(first + i) % kLatencyTraceLen];
    return n;
}

uint16_t LatencyTrace::Min() const {
    if (!count_) return 0;
    uint16_t v = 0xFFFF;
    for (size_t i = 0; i < count_; ++i) {
        const uint16_t s = samples_[(head_ + kLatencyTraceLen - count_ + i) % kLatencyTraceLen];
        if (s < v) v = s;
    }
    return v;
}

uint16_t LatencyTrace::Max() const {
    uint16_t v = 0;
    for (size_t i = 0; i < count_; ++i) {
        const uint16_t s = samples_[(head_ + kLatencyTraceLen - count_ + i) % kLatencyTraceLen];
        if (s > v) v = s;
    }
    return v;
}

uint16_t LatencyTrace::Last() const {
    if (!count_) return 0;
    return samples_[(head_ + kLatencyTraceLen - 1) % kLatencyTraceLen];
}

double LatencyTrace::Avg() const {
    if (!count_) return 0.0;
    uint64_t sum = 0;
    for (size_t i = 0; i < count_; ++i) {
        sum += samples_[(head_ + kLatencyTraceLen - count_ + i) % kLatencyTraceLen];
    }
    return double(sum) / double(count_);
}

void LatencyTrace::Clear() {
    head_ = 0;
    count_ = 0;
    pending_ = 0;
    havePending_ = false;
    haveMark_ = false;
}

} // namespace deskhub
