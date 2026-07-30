// =============================================================================
// ClockOffset.cpp — cài đặt bộ lọc min hai xô. Toàn bộ lý do của từng lựa chọn nằm
// ở ClockOffset.h; ở đây chỉ còn phép tính.
// =============================================================================
#include "deskhub/control/ClockOffset.h"

#include <algorithm>

namespace deskhub {

void ClockOffset::Reset() {
    haveSample_ = false;
    havePrev_ = false;
    curMin_ = prevMin_ = floor_ = lastRaw_ = 0;
    windowStartUs_ = 0;
}

// Đảo xô nếu cửa sổ hiện tại đã hết hạn. Dùng while chứ không if: nguồn tĩnh có thể
// im lặng lâu hơn một cửa sổ, và khi ấy phải nhảy qua HẾT các cửa sổ rỗng — bỏ sót
// thì một mẫu đơn độc sau 60 giây im lặng sẽ bị so với sàn của một phút trước.
void ClockOffset::Rotate(uint64_t localUs) {
    while (localUs - windowStartUs_ >= kWindowUs) {
        prevMin_ = curMin_;
        // Xô ĐANG ĐÓNG mà rỗng thì nó không có min nào để truyền xuống. Gán
        // havePrev_ = true vô điều kiện ở đây là lỗi đã gặp: sau vòng xoay thứ hai
        // của một quãng im lặng dài, prevMin_ nhận giá trị mặc định 0 và trở thành
        // một cái "sàn" giả — mọi mẫu sau đó bị báo trễ bằng đúng độ trễ thật của
        // nó thay vì 0.
        havePrev_ = !curEmpty_;
        windowStartUs_ += kWindowUs;
        curMin_ = 0;
        curEmpty_ = true;
    }
}

void ClockOffset::AddSample(uint64_t hostPtsUs, uint64_t localUs) {
    // Hiệu của hai đồng hồ khác gốc: có thể ÂM và rất lớn. int64_t bắt buộc —
    // uint64_t ở đây tràn thành số khổng lồ và sàn không bao giờ đúng nữa.
    const int64_t raw = int64_t(localUs) - int64_t(hostPtsUs);
    lastRaw_ = raw;

    if (!haveSample_) {
        haveSample_ = true;
        windowStartUs_ = localUs;
        curMin_ = raw;
        curEmpty_ = false;
        havePrev_ = false;
        floor_ = raw;
        return;
    }

    Rotate(localUs);

    if (curEmpty_) {
        curMin_ = raw;
        curEmpty_ = false;
    } else {
        curMin_ = std::min(curMin_, raw);
    }

    floor_ = havePrev_ ? std::min(curMin_, prevMin_) : curMin_;
}

int64_t ClockOffset::LatencyUs(uint64_t netFloorUs) const {
    if (!haveSample_) return -1;
    // lastRaw_ >= floor_ luôn đúng: mẫu vừa bơm đã được gộp vào curMin_, và floor_
    // không lớn hơn curMin_. Vẫn kẹp ở 0 để một thay đổi tương lai ở Rotate không
    // âm thầm đẻ ra số âm.
    const int64_t excess = std::max<int64_t>(0, lastRaw_ - floor_);
    return excess + int64_t(netFloorUs);
}

} // namespace deskhub
