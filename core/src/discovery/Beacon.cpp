// =============================================================================
// Beacon.cpp — cài đặt bộ trả lời hỏi-đáp một nhịp phía host.
//
// Cả file là một switch ba nhánh, và điều đáng nói không nằm ở chỗ nó LÀM gì mà ở
// chỗ nó KHÔNG làm gì: không giữ trạng thái giữa các lần gọi, không đụng vào phiên,
// không tự gửi. Nhờ vậy một trận lụt DISCOVER từ mạng chỉ tốn đúng chi phí dựng gói
// trả lời — nó không thể làm hỏng phiên đang chạy, và cũng không có gì để làm hỏng.
//
// LIÊN QUAN: deskhub/discovery/Beacon.h (vì sao tách khỏi HostSession)
// =============================================================================
#include "deskhub/discovery/Beacon.h"

namespace deskhub {

size_t Beacon::Reply(std::span<uint8_t> out, std::span<const uint8_t> pkt) const {
    const auto h = ParseCommonHeader(pkt);
    if (!h) return 0;
    const auto payload = PayloadOf(pkt);

    switch (h->type) {
        case MsgType::Discover: {
            const auto probeId = ParseDiscover(payload);
            if (!probeId) return 0;
            HostAnnounce a;
            a.probeId = *probeId; // dội lại: client bỏ được câu trả lời của lượt quét cũ
            a.hostId = id_.hostId;
            a.port = id_.port;
            a.flags = uint8_t((acceptsInput_ ? kAnnounceFlagAcceptsInput : 0) |
                              (busy_ ? kAnnounceFlagBusy : 0));
            // Cắt về u8 chứ không kẹp: kMaxSources = 8 nên không bao giờ tràn, và
            // để nó tự tràn ở đây sẽ là lỗi âm thầm nếu trần đó tăng lên sau này.
            a.sourceCount = uint8_t(sources_.size() > 0xFF ? 0xFF : sources_.size());
            a.name = id_.name;
            return BuildAnnounce(out, a);
        }
        case MsgType::ListSources:
            // Không có nguồn nào vẫn trả lời, với danh sách rỗng: im lặng bị client
            // hiểu là "host bản cũ / mất gói" và nó ngồi thử lại suốt 3 giây (§3c),
            // trong khi sự thật là host đang bật nhưng chưa tick nguồn nào.
            return BuildSourceList(out, sources_);
        case MsgType::Ping: {
            // CHỈ ping dò đường (sessionId = 0). Ping trong phiên là việc của
            // HostSession — nó phải nuôi timeout, còn cái này thì không.
            if (h->sessionId != 0) return 0;
            const auto m = ParsePingPong(payload);
            if (!m) return 0;
            // Dội nguyên văn payload: sendTimeUs là đồng hồ CLIENT nên nó tính RTT
            // bằng một phép trừ, hai đồng hồ không cần đồng bộ (xem BuildPingPongImpl).
            return BuildPong(out, 0, *m);
        }
        default:
            return 0;
    }
}

} // namespace deskhub
