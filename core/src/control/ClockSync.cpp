// =============================================================================
// ClockSync.cpp — cài đặt bộ lọc cực tiểu trượt theo cửa sổ.
//
// Ba dòng số học, nhưng thứ tự của chúng có ý nghĩa: cực tiểu của cửa sổ ĐANG chạy
// được nuôi song song với cực tiểu đang dùng, và chỉ thay thế khi hết cửa sổ. Nuôi
// một biến rồi mới đổi (thay vì reset thẳng về vô cực lúc hết cửa sổ) tránh được
// khoảng trống ngay sau mỗi lần làm mới — nếu reset, frame đầu tiên của cửa sổ mới
// sẽ tự nó thành cực tiểu và e2e nhảy về 0 mỗi 10 giây.
//
// LIÊN QUAN: deskhub/control/ClockSync.h (vì sao cực tiểu + nửa RTT)
// =============================================================================
#include "deskhub/control/ClockSync.h"

namespace deskhub {

void ClockSync::Rebase(uint64_t hostTimebaseUs, uint64_t localUs) {
    offsetUs_ = int64_t(localUs) - int64_t(hostTimebaseUs);
    windowMinUs_ = offsetUs_;
    haveOffset_ = true;
    haveWindowMin_ = true;
    windowStartUs_ = localUs;
}

void ClockSync::OnFrame(uint64_t hostTimestampUs, uint64_t localArrivalUs) {
    const int64_t sample = int64_t(localArrivalUs) - int64_t(hostTimestampUs);

    if (!haveOffset_) {
        offsetUs_ = sample;
        haveOffset_ = true;
        windowStartUs_ = localArrivalUs;
    }
    if (!haveWindowMin_ || sample < windowMinUs_) {
        windowMinUs_ = sample;
        haveWindowMin_ = true;
    }
    // Cực tiểu MỚI thắng ngay, không đợi hết cửa sổ: nó chỉ có thể nhỏ hơn khi ta vừa
    // gặp một frame đi nhanh hơn mọi frame trước, tức ước lượng cũ đã quá cao.
    if (sample < offsetUs_) offsetUs_ = sample;

    // Hết cửa sổ: nhận cực tiểu của cửa sổ vừa qua làm gốc mới. Đây là chiều DUY NHẤT
    // cho phép ước lượng đi lên — nhờ nó bám được đồng hồ trôi ngược (xem header).
    if (localArrivalUs - windowStartUs_ >= refreshUs_) {
        offsetUs_ = windowMinUs_;
        windowStartUs_ = localArrivalUs;
        haveWindowMin_ = false;
    }
}

uint32_t ClockSync::E2eUs(uint64_t hostTimestampUs, uint64_t localPresentUs) const {
    if (!haveOffset_) return 0;
    const int64_t raw = int64_t(localPresentUs) - int64_t(hostTimestampUs) - offsetUs_;
    // Cộng lại nửa RTT: offsetUs_ đã nuốt mất độ trễ một chiều nhỏ nhất, không hoàn
    // lại thì frame nhanh nhất luôn báo 0 ms.
    const int64_t est = raw + int64_t(rttUs_) / 2;
    if (est <= 0) return 0;
    // Kẹp trên: một phiên chạy nhiều giờ với đồng hồ trôi mạnh có thể cho hiệu vượt
    // u32. Con số đó vô nghĩa dù kẹp hay không, nhưng tràn âm thầm thì còn tệ hơn.
    constexpr int64_t kMax = 0xFFFFFFFF;
    return uint32_t(est > kMax ? kMax : est);
}

} // namespace deskhub
