#pragma once
// =============================================================================
// QualityLadder.h — chọn ĐỘ PHÂN GIẢI + FPS theo băng thông thực tế, phía HOST.
//
// NHIỆM VỤ
//   Trả lời "đường truyền chỉ tải nổi B bit/giây — vậy nên phát bao nhiêu pixel và
//   bao nhiêu khung hình một giây?". Vào là bitrate BitrateController vừa chốt được,
//   ra là một QualityStep. Không đụng encoder, không đụng capture, không đọc đồng hồ.
//
// VÌ SAO PHẢI CÓ LỚP NÀY — BITRATE MỘT MÌNH KHÔNG PHẢI LÀ CHẤT LƯỢNG
//   Con số quyết định hình có xem được hay không là BIT TRÊN MỖI ĐIỂM ẢNH MỖI KHUNG:
//
//       bpp = bitrate / (rộng × cao × fps)
//
//   Ba đại lượng người dùng chỉnh được (fps, bitrate, độ phân giải) chỉ có nghĩa khi
//   đứng CÙNG NHAU trong công thức đó. Trước đây chúng là ba núm rời:
//     - bitrate  thì thích nghi (BitrateController, AIMD theo mất gói),
//     - fps      thì KHÔNG BAO GIỜ đổi,
//     - độ phân giải chốt một lần lúc HELLO (FitStreamSize) rồi thôi.
//   Nên khi đường truyền xấu, cách duy nhất hệ thống phản ứng là hạ bitrate — giữ
//   nguyên số pixel và số khung. 1920×1246@60fps mà tụt xuống sàn 1 Mbps là
//   0.007 bpp: không phải "hình mờ", mà là cháo. Đúng lúc cần nhìn rõ nhất thì
//   không còn gì để nhìn.
//
//   Phản ứng đúng với đường truyền xấu là GIẢM THỨ ĐANG ĐÒI BĂNG THÔNG, không phải
//   ép bộ nén làm điều bất khả thi.
//
// ⚠ THỨ TỰ HẠ: FPS TRƯỚC, ĐỘ PHÂN GIẢI SAU
//   Đây là quyết định quan trọng nhất trong file, và nó phụ thuộc vào việc người ta
//   DÙNG cái này để làm gì. Deskhub là công cụ điều khiển máy từ xa: nội dung chủ
//   yếu là chữ, cửa sổ, dòng lệnh. Chữ mờ thì không đọc được, và không đọc được thì
//   phiên coi như hỏng. Chữ nét ở 20fps thì vẫn làm việc bình thường.
//   Nên: giữ pixel tới cùng, bỏ khung hình trước. (Cùng lựa chọn với
//   degradationPreference = maintain-resolution mà WebRTC đặt mặc định cho chia sẻ
//   màn hình, khác với gọi video.)
//
//   Nhưng KHÔNG hạ fps xuống dưới ~20: dưới ngưỡng đó con trỏ chuột bắt đầu nhảy
//   cóc và cảm giác điều khiển hỏng hẳn, bất kể độ trễ thật bao nhiêu. Từ 20fps trở
//   xuống thì phải co pixel. Bậc cuối cùng (nửa độ phân giải, 12fps) là phương án
//   chót cho đường truyền tệ tới mức nếu không có nó thì chỉ còn màn hình đen.
//
// VÌ SAO HAI CHIỀU KHÔNG ĐỐI XỨNG
//   TỤT thì nhảy thẳng tới bậc đúng, ngay lập tức: hàng đợi đang đầy, mỗi giây chần
//   chừ là thêm gói mất. LÊN thì mỗi lần một bậc, phải dư 20% băng thông và phải giữ
//   được như thế vài giây. Dò lên quá tay chỉ tạo lại đúng cái nghẽn vừa thoát ra —
//   cùng lý do BitrateController tụt ×0.75 mà lên +5%.
//
//   Riêng bậc có ĐỔI ĐỘ PHÂN GIẢI còn đắt hơn nữa: nó bắt dựng lại encoder, phát một
//   IDR (nặng gấp hàng chục lần P-frame), gửi RECONFIG, và client phải dựng lại bộ
//   giải mã. Nhấp nháy giữa hai bậc độ phân giải còn hại hơn ở lì bậc thấp.
//
// LIÊN QUAN: deskhub/control/BitrateController.h (quyết định B — đầu vào của lớp này),
//            deskhub/control/StreamSize.h (quyết định TRẦN — đầu vào còn lại),
//            deskhub/protocol/Wire.h (Reconfig mang w/h/fps/bitrate tới client),
//            docs/09-diagnostics.md
// =============================================================================
#include <cstdint>

namespace deskhub {

struct QualityStep {
    uint16_t width = 0, height = 0;
    uint8_t fps = 0;
    // Phần trăm so với TRẦN độ phân giải. Đây mới là thứ caller nên truyền xuống tầng
    // capture, chứ không phải width/height: trần có thể đổi giữa phiên (người dùng đổi
    // độ phân giải màn hình nguồn) và khi đó w/h của thang thành số cũ, còn "50% của
    // trần hiện tại" thì luôn đúng.
    uint8_t scalePct = 100;

    friend bool operator==(const QualityStep& a, const QualityStep& b) {
        return a.width == b.width && a.height == b.height && a.fps == b.fps &&
               a.scalePct == b.scalePct;
    }
};

class QualityLadder {
public:
    // Bit trên mỗi điểm ảnh mỗi khung mà ta nhắm tới, dạng phân số để phép tính là
    // số nguyên. 0.08 bpp là mức nội dung màn hình (chữ + ô cửa sổ, nhiều mảng phẳng,
    // ít nhiễu) còn nét ở H.264 — thấp hơn hẳn mức cần cho video quay thật.
    static constexpr uint32_t kBppNum = 8, kBppDen = 100;

    // Phải dư ngần này mới cho lên bậc, và phải giữ được suốt kUpDwellUs.
    static constexpr uint32_t kUpHeadroomPct = 120;
    static constexpr uint64_t kUpDwellUs = 5'000'000;
    // Bậc phải ĐỔI ĐỘ PHÂN GIẢI thì chờ lâu hơn: nó kéo theo dựng lại encoder + IDR
    // + client dựng lại decoder. Nhấp nháy độ phân giải hại hơn ở lì bậc thấp.
    static constexpr uint64_t kUpDwellResizeUs = 15'000'000;

    // maxW/maxH: trần độ phân giải đã chốt lúc bắt tay (FitStreamSize) — bậc cao
    // nhất của thang. maxFps: trần fps người dùng đặt. Thang KHÔNG BAO GIỜ vượt hai
    // con số này; nó chỉ đi xuống từ đó.
    QualityLadder(uint16_t maxW, uint16_t maxH, uint8_t maxFps);

    // Gọi mỗi khi có Feedback (~1 lần/giây). `bitrateBps` là mức BitrateController
    // vừa chốt được. Trả true nếu bậc VỪA ĐỔI — khi đó caller phải cấu hình lại
    // capture/encoder và gửi RECONFIG.
    //
    // So current() trước và sau để biết đổi cái gì: chỉ fps đổi là thay đổi RẺ
    // (không dựng lại encoder); w/h đổi là thay đổi ĐẮT.
    bool Update(uint32_t bitrateBps, uint64_t nowUs);

    QualityStep current() const {
        return step_;
    }

    // Băng thông tối thiểu bậc hiện tại cần để đạt kBpp. Chỉ để chẩn đoán/kiểm thử.
    uint32_t requiredBps() const {
        return RequiredBps(rung_);
    }

    // Số bậc của thang (khác nhau tuỳ maxFps — maxFps thấp làm vài bậc trên trùng
    // nhau và bị gộp). Chỉ để kiểm thử.
    int rungCount() const {
        return count_;
    }
    int rung() const {
        return rung_;
    }

private:
    QualityStep StepAt(int rung) const;
    uint32_t RequiredBps(int rung) const;
    // Bậc THẤP NHẤT (chất lượng cao nhất) mà `bitrateBps` gánh nổi.
    int BestRungFor(uint32_t bitrateBps) const;

    uint16_t maxW_, maxH_;
    uint8_t maxFps_;
    int count_ = 0;  // số bậc dùng được sau khi gộp trùng
    int rung_ = 0;   // bậc hiện tại; 0 = tốt nhất
    QualityStep step_{};
    uint64_t lastChangeUs_ = 0;
    // Đã đủ dư băng thông để lên bậc từ lúc nào (0 = đang không đủ). Tách khỏi
    // lastChangeUs_ vì hai điều kiện độc lập: "đã ổn định đủ lâu" và "vừa đổi xong".
    uint64_t upSinceUs_ = 0;
};

} // namespace deskhub
