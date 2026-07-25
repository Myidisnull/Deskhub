#pragma once
// =============================================================================
// ClockSync.h — quy đổi đồng hồ HOST sang đồng hồ CLIENT để đo trễ đầu-cuối.
//
// BÀI TOÁN
//   Overlay của client hiện "e2e 11 ms": khoảng thời gian từ lúc host CHỤP một frame
//   tới lúc client HIỆN nó. Host đóng dấu thời gian chụp vào VideoHeader::timestampUs
//   bằng đồng hồ CỦA NÓ, client đọc đồng hồ của mình lúc hiện — hai con số này ở hai
//   hệ quy chiếu khác nhau, trừ thẳng cho nhau là ra một số vô nghĩa (thường là vài
//   giờ, vì hai máy khởi động cách nhau vài giờ).
//
//   Cần biết ĐỘ LỆCH giữa hai đồng hồ. Nhưng độ lệch đó không đo trực tiếp được: mọi
//   gói mang timestamp của host đều mất một quãng đường mới tới nơi, và ta không tách
//   được "đồng hồ lệch bao nhiêu" khỏi "gói đi mất bao lâu".
//
// CÁCH GIẢI: BỘ LỌC CỰC TIỂU + NỬA RTT
//   Với mỗi frame, đại lượng (giờ_client_lúc_nhận − dấu_thời_gian_host) = độ lệch
//   đồng hồ + độ trễ một chiều của riêng frame đó. Số hạng thứ hai luôn ≥ 0 và thay
//   đổi theo từng gói, nên CỰC TIỂU của cả dãy là ước lượng tốt nhất cho "độ lệch
//   đồng hồ + độ trễ một chiều nhỏ nhất có thể".
//
//   Lấy cực tiểu đó làm gốc, rồi cộng lại rtt/2 làm ước lượng cho chính cái độ trễ
//   một chiều nhỏ nhất vừa bị trừ mất. Không cộng lại thì frame nhanh nhất luôn báo
//   e2e = 0 — một con số đẹp và sai.
//
//   ⚠️ Đây là ƯỚC LƯỢNG, không phải phép đo. Nó giả định đường đi và đường về đối
//   xứng (rtt/2 mỗi chiều). Đường bất đối xứng — hay gặp trên Wi-Fi và trên
//   Tailscale — làm con số lệch đi đúng bằng phần bất đối xứng đó. Muốn đo THẬT thì
//   phải đồng bộ đồng hồ hai máy (PTP/NTP), việc đó nằm ngoài phạm vi v1.
//
// VÌ SAO CỬA SỔ CỰC TIỂU PHẢI ĐƯỢC LÀM MỚI
//   Cực tiểu tích luỹ từ đầu phiên chỉ có thể GIẢM. Đồng hồ hai máy trôi lệch nhau
//   (thạch anh rẻ tiền lệch vài chục ppm — hàng chục mili-giây mỗi giờ); nếu đồng hồ
//   host chạy nhanh dần so với client, cực tiểu cũ trở nên quá nhỏ và e2e báo cao dần
//   một cách giả tạo. Nên cực tiểu chỉ giữ trong `refreshUs`, hết cửa sổ thì thay
//   bằng cực tiểu của cửa sổ vừa qua — bám được cả hai chiều trôi.
//
// MÔ HÌNH LUỒNG
//   Không khoá. OnFrame/E2eUs chạy trên luồng decode-render, OnRtt trên luồng Recv —
//   caller phải tự đồng bộ nếu hai luồng đó khác nhau. Thực tế RTT chỉ là một số
//   nguyên đọc/ghi nguyên tử trên mọi kiến trúc ta hỗ trợ, và sai một nhịp cập nhật
//   chỉ làm lệch con số hiển thị chứ không hỏng gì.
//
// LIÊN QUAN: deskhub/control/LinkStats.h (gom e2e theo cửa sổ 1 giây),
//            deskhub/control/LatencyTrace.h (dãy mẫu vẽ biểu đồ),
//            deskhub/wire/Wire.h (HelloAck::timebaseUs, VideoHeader::timestampUs)
// =============================================================================
#include <cstdint>

namespace deskhub {

// Cực tiểu giữ trong bao lâu trước khi thay bằng cực tiểu của cửa sổ kế. 10 giây đủ
// dài để gom được một frame "may mắn" đi nhanh, đủ ngắn để bám kịp đồng hồ trôi.
inline constexpr uint64_t kClockRefreshUs = 10'000'000;

class ClockSync {
public:
    explicit ClockSync(uint64_t refreshUs = kClockRefreshUs) : refreshUs_(refreshUs) {}

    // HELLO_ACK: `hostTimebaseUs` là đồng hồ host lúc nó dựng ACK, `localUs` là đồng
    // hồ ta lúc nhận. Chỉ để GIEO một giá trị ban đầu — nhờ nó những frame đầu tiên
    // đã có số để hiện, thay vì một ô trống trong vài giây đầu phiên. Frame về sau sẽ
    // tự chỉnh lại qua bộ lọc cực tiểu.
    void Rebase(uint64_t hostTimebaseUs, uint64_t localUs);

    // Mỗi PONG. rtt/2 là ước lượng độ trễ một chiều nhỏ nhất, xem ghi chú đầu file.
    void OnRtt(uint32_t rttUs) {
        rttUs_ = rttUs;
    }

    // Mỗi frame VỪA GHÉP XONG: nuôi bộ lọc cực tiểu. Gọi với thời điểm NHẬN (không
    // phải thời điểm hiện) — ta muốn cực tiểu của độ trễ MẠNG, không lẫn thời gian
    // giải mã và xếp hàng hiển thị của chính máy mình.
    void OnFrame(uint64_t hostTimestampUs, uint64_t localArrivalUs);

    // Đã đủ dữ liệu để E2eUs có nghĩa chưa (đã Rebase hoặc đã thấy ≥1 frame).
    bool ready() const {
        return haveOffset_;
    }

    // Trễ đầu-cuối ước lượng của một frame, tính bằng micro-giây. 0 nếu chưa ready().
    // Kẹp dưới ở 0: một frame đi nhanh bất thường sau khi cực tiểu vừa được làm mới
    // có thể cho hiệu âm, mà "trễ âm" trên overlay thì vô nghĩa hơn là 0.
    uint32_t E2eUs(uint64_t hostTimestampUs, uint64_t localPresentUs) const;

    uint32_t rttUs() const {
        return rttUs_;
    }

private:
    // Cực tiểu của (giờ client − dấu thời gian host). Có dấu: đồng hồ host có thể
    // lớn hơn đồng hồ client (hai máy khởi động khác giờ), hiệu khi đó là số âm.
    int64_t offsetUs_ = 0;
    int64_t windowMinUs_ = 0; // cực tiểu của cửa sổ ĐANG chạy — sẽ thay offsetUs_
    bool haveOffset_ = false;
    bool haveWindowMin_ = false;
    uint64_t windowStartUs_ = 0;
    uint64_t refreshUs_;
    uint32_t rttUs_ = 0;
};

} // namespace deskhub
