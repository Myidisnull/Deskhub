#pragma once
// =============================================================================
// Beacon.h — trả lời những gói KHÔNG THUỘC PHIÊN nào, phía host.
//
// NHIỆM VỤ
//   Ba câu hỏi mà client đặt ra TRƯỚC khi có phiên, và chỉ ba câu này:
//
//     DISCOVER      → ANNOUNCE     "có host nào ở đây không?"  (dò trong mạng LAN)
//     LIST_SOURCES  → SOURCE_LIST  "máy đó đang chia sẻ gì?"   (chọn nguồn)
//     PING sid=0    → PONG  sid=0  "máy đó còn sống? bao xa?"  (thẻ máy đã lưu)
//
//   Cả ba đều là hỏi-đáp một nhịp, không trạng thái, và đều nuôi đúng một phần của
//   giao diện: danh sách "Found on this network", danh sách nguồn, và cặp số
//   sống/độ trễ trên mỗi thẻ máy đã lưu.
//
// VÌ SAO KHÔNG NHÉT VÀO HostSession
//   Ba lý do, và lý do thứ ba là lý do thật:
//     1. Chúng đến khi CHƯA có phiên — HostSession lúc đó đang ở IDLE và theo thiết
//        kế nó bỏ hết mọi thứ không phải HELLO.
//     2. Chúng KHÔNG được nuôi timeout của phiên. Một máy lạ trong mạng dò đường
//        không được phép giữ cho một phiên đã chết sống thêm.
//     3. Quan trọng nhất: chúng phải trả lời về ĐÚNG NƠI VỪA HỎI, không phải về peer
//        của phiên. HostSession gửi qua callback `send` vốn trỏ tới peer đang kết
//        nối; đáp DISCOVER qua đường đó là bắn câu trả lời vào mặt một client khác.
//        Nên Beacon không gửi gì cả — nó chỉ DỰNG byte vào `out`, còn caller
//        sendto() về địa chỉ nguồn của datagram nó vừa nhận.
//
// CÁCH DÙNG TRONG VÒNG LẶP NHẬN CỦA HOST
//     if (size_t n = beacon.Reply(buf, pkt); n) { sendto(from, buf, n); continue; }
//     if (host.HandlePacket(pkt, nowUs)) peer = from;
//   Beacon đi TRƯỚC: các gói này không bao giờ thuộc phiên nên đưa vào HostSession
//   chỉ tốn một lần parse rồi bị bỏ.
//
// MÔ HÌNH LUỒNG
//   Không có khoá. Reply() chỉ đọc, các hàm Set* chỉ ghi — caller gọi cả hai trên
//   CÙNG thread Recv (giống HostSession). Danh sách nguồn đổi trên thread khác thì
//   caller phải tự đưa về thread Recv, đừng gọi SetSources() từ thread capture.
//
// LIÊN QUAN: deskhub/wire/Wire.h (DISCOVER/ANNOUNCE/SOURCE_LIST),
//            deskhub/discovery/HostRegistry.h (đầu kia — client gom ANNOUNCE),
//            docs/04-protocol.md §3d
// =============================================================================
#include "deskhub/wire/Wire.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace deskhub {

// Máy này là ai, nhìn từ mạng.
struct HostIdentity {
    // Ổn định theo MÁY, không theo lần chạy: client gộp nhiều ANNOUNCE của cùng một
    // máy (đến từ nhiều card mạng) theo trường này. Đổi mỗi lần khởi động thì mỗi
    // lần mở app lại thành một máy mới trong danh sách đã lưu.
    uint32_t hostId = 0;
    uint16_t port = 0; // cổng đang nghe — client cần số này để dựng địa chỉ kết nối
    std::string name;  // tên máy (UTF-8), cắt ở kMaxHostNameBytes khi lên dây
};

class Beacon {
public:
    void SetIdentity(HostIdentity id) {
        id_ = std::move(id);
    }

    // Ảnh chụp danh sách nguồn đang chia sẻ. Chép ra chứ không giữ con trỏ: caller
    // dựng vector này từ bộ đếm nguồn của nó rồi thả ngay.
    void SetSources(std::span<const SourceInfo> sources) {
        sources_.assign(sources.begin(), sources.end());
    }

    // Đã có client → ANNOUNCE mang cờ Busy. Client vẽ thẻ máy xám thay vì để người
    // dùng bấm vào rồi mới ăn HELLO_ACK từ chối.
    void SetBusy(bool on) {
        busy_ = on;
    }

    // Host có cho điều khiển không (ô "Allow keyboard and mouse"). Đi vào ANNOUNCE
    // để client biết TRƯỚC khi kết nối rằng phiên sẽ là chỉ-xem.
    void SetAcceptsInput(bool on) {
        acceptsInput_ = on;
    }

    // Dựng câu trả lời cho `pkt` vào `out`. Trả số byte đã ghi, hoặc 0 nếu gói này
    // không phải việc của Beacon (mọi thứ khác — HELLO, INPUT, NACK… — thuộc về
    // HostSession) hoặc `out` không đủ chỗ. `out` nên có kMaxDatagram byte.
    size_t Reply(std::span<uint8_t> out, std::span<const uint8_t> pkt) const;

    const HostIdentity& identity() const {
        return id_;
    }

private:
    HostIdentity id_{};
    std::vector<SourceInfo> sources_;
    bool busy_ = false;
    bool acceptsInput_ = true;
};

} // namespace deskhub
