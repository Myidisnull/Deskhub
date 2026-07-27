#pragma once
// =============================================================================
// Beacon.h — trả lời những gói KHÔNG THUỘC PHIÊN nào, phía host.
//
// NHIỆM VỤ
//   Hai câu hỏi mà client đặt ra TRƯỚC khi có phiên, và chỉ hai câu này:
//
//     LIST_SOURCES  → SOURCE_LIST  "máy đó đang chia sẻ gì?"   (chọn nguồn)
//     PING sid=0    → PONG  sid=0  "máy đó còn sống? bao xa?"  (thẻ máy đã lưu)
//
//   Cả hai đều là hỏi-đáp một nhịp, không trạng thái, và đều nuôi đúng một phần
//   của giao diện: danh sách nguồn, và cặp số sống/độ trễ trên mỗi thẻ máy đã lưu.
//
// VÌ SAO KHÔNG NHÉT VÀO HostSession
//   Ba lý do, và lý do thứ ba là lý do thật:
//     1. Chúng đến khi CHƯA có phiên — HostSession lúc đó đang ở IDLE và theo thiết
//        kế nó bỏ hết mọi thứ không phải HELLO.
//     2. Chúng KHÔNG được nuôi timeout của phiên. Một máy lạ trong mạng dò đường
//        không được phép giữ cho một phiên đã chết sống thêm.
//     3. Quan trọng nhất: chúng phải trả lời về ĐÚNG NƠI VỪA HỎI, không phải về peer
//        của phiên. HostSession gửi qua callback `send` vốn trỏ tới peer đang kết
//        nối; đáp LIST_SOURCES qua đường đó là bắn câu trả lời vào mặt một client
//        khác. Nên Beacon không gửi gì cả — nó chỉ DỰNG byte vào `out`, còn caller
//        sendto() về địa chỉ nguồn của datagram nó vừa nhận.
//
// CÁCH DÙNG TRONG VÒNG LẶP NHẬN CỦA HOST
//     if (size_t n = beacon.Reply(buf, pkt); n) { sendto(from, buf, n); continue; }
//     if (host.HandlePacket(pkt, nowUs)) peer = from;
//   Beacon đi TRƯỚC: các gói này không bao giờ thuộc phiên nên đưa vào HostSession
//   chỉ tốn một lần parse rồi bị bỏ.
//
// MÔ HÌNH LUỒNG
//   Không có khoá. Reply() chỉ đọc, SetSources() chỉ ghi — caller gọi cả hai trên
//   CÙNG thread Recv (giống HostSession). Danh sách nguồn đổi trên thread khác thì
//   caller phải tự đưa về thread Recv, đừng gọi SetSources() từ thread capture.
//
// LIÊN QUAN: deskhub/protocol/Wire.h (SOURCE_LIST/PING), docs/04-protocol.md §3d
// =============================================================================
#include "deskhub/protocol/Wire.h"

#include <cstdint>
#include <span>
#include <vector>

namespace deskhub {

class Beacon {
public:
    // Ảnh chụp danh sách nguồn đang chia sẻ. Chép ra chứ không giữ con trỏ: caller
    // dựng vector này từ bộ đếm nguồn của nó rồi thả ngay.
    void SetSources(std::span<const SourceInfo> sources) {
        sources_.assign(sources.begin(), sources.end());
    }

    // Dựng câu trả lời cho `pkt` vào `out`. Trả số byte đã ghi, hoặc 0 nếu gói này
    // không phải việc của Beacon (mọi thứ khác — HELLO, INPUT, NACK… — thuộc về
    // HostSession) hoặc `out` không đủ chỗ. `out` nên có kMaxDatagram byte.
    size_t Reply(std::span<uint8_t> out, std::span<const uint8_t> pkt) const;

private:
    std::vector<SourceInfo> sources_;
};

} // namespace deskhub
