// =============================================================================
// QualityLadder.cpp — cài đặt. Toàn bộ lý do của từng ngưỡng nằm ở QualityLadder.h;
// ở đây chỉ còn phép tính và bảng bậc thang.
// =============================================================================
#include "deskhub/control/QualityLadder.h"

namespace {

// Bảng bậc thang, từ TỐT NHẤT xuống TỆ NHẤT. `scalePct` áp lên trần độ phân giải,
// `fps` bị kẹp bởi trần fps người dùng.
//
// Ba bậc đầu giữ nguyên pixel và chỉ bỏ khung hình — xem ⚠ ở header về vì sao chữ
// nét quan trọng hơn khung mượt với một công cụ điều khiển từ xa. 20fps là sàn của
// giai đoạn đó: dưới ngưỡng ấy cảm giác điều khiển hỏng.
struct Rung {
    uint8_t scalePct;
    uint8_t fps;
};

constexpr Rung kRungs[] = {
    {100, 60}, // đường truyền tốt: đúng những gì người dùng đặt
    {100, 30}, // bỏ nửa số khung, giữ trọn pixel
    {100, 20}, // sàn fps của giai đoạn "giữ pixel"
    {75, 20},  // từ đây mới bắt đầu co pixel
    {50, 20},
    {50, 12}, // phương án chót: dưới mức này thì thà không stream
};
constexpr int kRungTotal = int(sizeof(kRungs) / sizeof(kRungs[0]));

// Cỡ nhỏ nhất còn nén được. Đối ứng kMinEncodeW/H phía agent — bộ nén phần cứng từ
// chối khung quá nhỏ, và một bậc thang không nén nổi thì tệ hơn hẳn bậc trên nó.
constexpr uint32_t kMinW = 160, kMinH = 64;

uint16_t ScaleEven(uint16_t v, uint8_t pct) {
    const uint32_t s = (uint32_t(v) * pct) / 100u;
    return uint16_t(s & ~1u); // H.264 lấy mẫu chroma theo khối 2×2 -> cạnh phải chẵn
}

} // namespace

namespace deskhub {

QualityLadder::QualityLadder(uint16_t maxW, uint16_t maxH, uint8_t maxFps)
    : maxW_(maxW), maxH_(maxH), maxFps_(maxFps ? maxFps : 60) {
    // Đếm số bậc DÙNG ĐƯỢC. Hai lý do một bậc bị loại:
    //   1. Trùng bậc trước sau khi kẹp fps — người dùng đặt 30fps thì {100,60} và
    //      {100,30} ra cùng một thứ; giữ cả hai chỉ làm thang có bậc chết mà thuật
    //      toán lên/xuống vẫn phải bước qua.
    //   2. Co xuống dưới mức nén được.
    // Thang luôn có ít nhất một bậc: bậc 0 chính là trần, và nếu ngay cả trần cũng
    // dưới kMinW/kMinH thì đó là chuyện của tầng trên (agent tạm dừng nguồn), không
    // phải chuyện của thang.
    QualityStep prev{};
    for (int i = 0; i < kRungTotal; ++i) {
        const QualityStep s = StepAt(i);
        if (i > 0) {
            if (s == prev) continue;
            if (s.width < kMinW || s.height < kMinH) break;
        }
        prev = s;
        ++count_;
    }
    step_ = StepAt(0);
}

QualityStep QualityLadder::StepAt(int rung) const {
    if (rung < 0) rung = 0;
    if (rung >= kRungTotal) rung = kRungTotal - 1;
    const Rung& r = kRungs[rung];
    QualityStep s;
    s.width = ScaleEven(maxW_, r.scalePct);
    s.height = ScaleEven(maxH_, r.scalePct);
    s.fps = r.fps < maxFps_ ? r.fps : maxFps_;
    s.scalePct = r.scalePct;
    return s;
}

uint32_t QualityLadder::RequiredBps(int rung) const {
    const QualityStep s = StepAt(rung);
    // uint64 bắt buộc: 1920×1246×60 vượt 32 bit trước cả khi nhân với tử số.
    const uint64_t pixelsPerSec = uint64_t(s.width) * s.height * s.fps;
    return uint32_t((pixelsPerSec * kBppNum) / kBppDen);
}

int QualityLadder::BestRungFor(uint32_t bitrateBps) const {
    for (int i = 0; i < count_; ++i)
        if (bitrateBps >= RequiredBps(i)) return i;
    // Không bậc nào gánh nổi -> bậc tệ nhất. Thà hình nhỏ và chậm còn hơn không có.
    return count_ - 1;
}

bool QualityLadder::Update(uint32_t bitrateBps, uint64_t nowUs) {
    const int want = BestRungFor(bitrateBps);

    // --- TỤT: nhảy thẳng, không chờ. Hàng đợi đang đầy. ---
    if (want > rung_) {
        rung_ = want;
        step_ = StepAt(rung_);
        lastChangeUs_ = nowUs;
        upSinceUs_ = 0;
        return true;
    }

    if (rung_ == 0) {
        upSinceUs_ = 0;
        return false; // đã ở trần, không có gì để lên
    }

    // --- LÊN: mỗi lần MỘT bậc, phải dư băng thông và phải giữ được đủ lâu. ---
    const int next = rung_ - 1;
    const uint64_t need = (uint64_t(RequiredBps(next)) * kUpHeadroomPct) / 100u;
    if (bitrateBps < need) {
        upSinceUs_ = 0; // tụt lại dưới ngưỡng -> đếm lại từ đầu
        return false;
    }

    if (!upSinceUs_) {
        upSinceUs_ = nowUs;
        return false;
    }

    // Bậc kế có đổi độ phân giải không quyết định phải chờ bao lâu — đổi pixel bắt
    // dựng lại encoder + IDR + client dựng lại decoder, đổi mỗi fps thì không.
    const QualityStep cand = StepAt(next);
    const bool resize = cand.width != step_.width || cand.height != step_.height;
    const uint64_t dwell = resize ? kUpDwellResizeUs : kUpDwellUs;
    if (nowUs - upSinceUs_ < dwell) return false;
    if (nowUs - lastChangeUs_ < dwell) return false;

    rung_ = next;
    step_ = cand;
    lastChangeUs_ = nowUs;
    upSinceUs_ = 0;
    return true;
}

} // namespace deskhub
