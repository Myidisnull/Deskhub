// =============================================================================
// LinkStats.cpp — cài đặt phép lấy hiệu giữa hai ảnh chụp bộ đếm.
//
// Toàn bộ file là hai hàm ngắn, nhưng ý tưởng cần nắm là:
//
//   Close() KHÔNG chỉ đọc số — nó còn CHỐT ảnh chụp mới (prev_ = cur) và dời mốc
//   thời gian (lastUs_ = nowUs). Nghĩa là gọi Close() hai lần liên tiếp sẽ cho lần
//   thứ hai toàn số 0. Đây là hàm có TÁC DỤNG PHỤ, không phải hàm truy vấn — người
//   gọi phải kiểm tra Due() trước và chỉ gọi một lần cho mỗi cửa sổ.
//
//   Mọi trường "…InWindow" trong LinkWindow là HIỆU so với lần đóng trước, trừ
//   lossRunMax — nó là kỷ lục tích luỹ từ đầu phiên nên chép thẳng, không lấy hiệu.
//
// LIÊN QUAN: deskhub/control/LinkStats.h (thiết kế + lý do tách lớp)
// =============================================================================
#include "deskhub/control/LinkStats.h"

namespace deskhub {

void LinkStats::AddE2e(uint32_t e2eUs) {
    e2eSumUs_ += e2eUs;
    if (e2eUs > e2eMaxUs_) e2eMaxUs_ = e2eUs;
    ++e2eCount_;
}

LinkWindow LinkStats::Close(const Reassembler::Stats& cur, uint64_t videoBytes,
    uint32_t renderedFrames, uint64_t nowUs) {
    LinkWindow w;
    // Dùng độ dài THẬT của cửa sổ chứ không phải windowUs_: vòng lặp client bị
    // recvfrom chặn tới 100ms nên cửa sổ hay dài hơn 1s một chút, chia theo hằng số
    // sẽ thổi phồng fps/kbps.
    const uint64_t elapsedUs = nowUs - lastUs_;
    w.secs = elapsedUs / 1e6;

    // Lấy hiệu so với ảnh chụp lần trước. Trừ số không dấu ở đây an toàn vì các bộ
    // đếm của Reassembler chỉ tăng, không bao giờ giảm hay bị reset giữa phiên.
    w.packetsReceived = cur.packetsReceived - prev_.packetsReceived;
    w.packetsLost = cur.packetsLost - prev_.packetsLost;
    w.packetsRecovered = cur.packetsRecovered - prev_.packetsRecovered;
    w.framesDropped = cur.framesDropped - prev_.framesDropped;

    for (size_t i = 0; i < 7; ++i) {
        w.lossRuns[i] = cur.lossRuns[i] - prev_.lossRuns[i];
        w.lossRunTotal += w.lossRuns[i];
    }
    w.lossRunMax = cur.lossRunMax;

    // Gói về muộn: đếm theo cửa sổ, độ muộn trung bình tính trên đúng cửa sổ này.
    w.latePackets = cur.latePackets - prev_.latePackets;
    const uint64_t lateMsInWin = cur.lateMsSum - prev_.lateMsSum;
    w.lateMsAvg = w.latePackets ? double(lateMsInWin) / double(w.latePackets) : 0.0;
    w.lateMsMax = cur.lateMsMax;

    // Mẫu số là số gói LẼ RA phải nhận = nhận được + mất. Bảo vệ phép chia cho 0:
    // giây đầu tiên sau khi kết nối thường chưa có gói video nào.
    const uint64_t seen = w.packetsReceived + w.packetsLost;
    w.lossPct = seen ? 100.0 * double(w.packetsLost) / double(seen) : 0.0;

    if (w.secs > 0.0) {
        w.fps = renderedFrames / w.secs;
        w.kbps = videoBytes * 8.0 / 1000.0 / w.secs;
    }

    // Trễ e2e: quy từ micro-giây sang mili-giây ở đây (một chỗ) thay vì ở overlay
    // của từng client. e2eSamples = 0 nghĩa là giây này không hiện được frame nào —
    // giao diện phải vẽ "—" chứ không phải "0 ms", hai thứ đó nghĩa ngược nhau.
    w.e2eSamples = e2eCount_;
    if (e2eCount_) {
        w.e2eMsAvg = double(e2eSumUs_) / double(e2eCount_) / 1000.0;
        w.e2eMsMax = uint32_t((e2eMaxUs_ + 500) / 1000); // làm tròn tới ms gần nhất
    }
    e2eSumUs_ = 0;
    e2eMaxUs_ = 0;
    e2eCount_ = 0;

    // Chốt ảnh chụp và mốc thời gian cho cửa sổ kế tiếp — xem ghi chú về tác dụng
    // phụ ở đầu file.
    prev_ = cur;
    lastUs_ = nowUs;
    return w;
}

// Nén cửa sổ vừa đóng vào 9 byte của gói FEEDBACK. Mọi trường đều bị thu hẹp kiểu:
// các con số này chỉ để host điều chỉnh bitrate theo bậc thang thô, không cần độ
// chính xác cao, và kênh control phải nhẹ vì nó chạy song song với luồng video.
Feedback MakeFeedback(const LinkWindow& w, uint32_t rttUs) {
    Feedback fb;
    fb.lostFrames = uint16_t(w.framesDropped);
    fb.lossPct = uint8_t(w.lossPct + 0.5); // làm tròn, kênh chỉ có 1 byte
    fb.rttMs = uint16_t(rttUs / 1000);
    fb.recvBitrateKbps = uint32_t(w.kbps);
    return fb;
}

} // namespace deskhub
