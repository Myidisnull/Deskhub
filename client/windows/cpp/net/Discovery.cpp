// =============================================================================
// Discovery.cpp — cài đặt một lượt quét.
//
// HÌNH DẠNG VÒNG LẶP
//   Phát DISCOVER ba lần, cách nhau đều trong cửa sổ thời gian, và nghe LIÊN TỤC
//   suốt cửa sổ đó. Phát nhiều lần vì DISCOVER đi UDP broadcast — thứ bị rơi nhiều
//   nhất trong mọi loại gói (switch không đảm bảo gì cho broadcast, và Wi-Fi phát
//   nó ở tốc độ cơ sở). Mất một lượt mà máy vẫn hiện ra là khác biệt giữa "danh
//   sách ổn định" và "danh sách nhấp nháy".
//
//   Không phát lại cho từng máy đã thấy: HostRegistry gộp theo hostId nên câu trả
//   lời lặp là vô hại, và ta không biết trước có bao nhiêu máy để mà đếm đủ.
//
// RTT ĐO TỪ ĐÂU
//   Từ lần phát GẦN NHẤT trước khi câu trả lời về, không phải từ đầu lượt quét —
//   nếu không, máy trả lời sau lượt phát thứ ba sẽ bị tính cả thời gian chờ giữa
//   các lượt và báo RTT hàng trăm mili-giây.
//
// LIÊN QUAN: net/Discovery.h (vì sao là hàm chặn, cái gì không tìm được)
// =============================================================================
#include "net/Discovery.h"

#include "net/HostIdent.h"
#include "net/NetInfo.h"
#include "net/UdpSocket.h"

#include "deskhub/discovery/HostRegistry.h"
#include "deskhub/wire/Wire.h"
#include "deskhubp/Clock.h"

namespace {

constexpr int kProbeBursts = 3;

// "1.2.3.4:47777" từ IP host-order + cổng.
std::string FormatAddr(uint32_t ipHost, uint16_t port) {
    return NetAddr{ipHost, port}.ToString();
}

} // namespace

std::vector<DiscoveredMachine> ScanForHosts(uint16_t hostPort, uint32_t windowMs) {
    std::vector<DiscoveredMachine> out;
    if (!hostPort) hostPort = 47777;
    if (windowMs < 200) windowMs = 200;

    UdpSocket sock;
    // Cổng 0 = hệ thống cấp cổng tạm. Host trả lời về CHÍNH cổng nguồn này, nên ta
    // không cần (và không được) chiếm cổng 47777 — máy này có thể đang tự chia sẻ.
    if (!sock.Open(0)) return out;
    if (!sock.SetBroadcast(true)) return out;
    sock.SetRecvTimeout(60); // nhịp đủ mau để bám sát lịch phát

    const auto casts = ListLocalBroadcasts();
    if (casts.empty()) return out;

    deskhub::HostRegistry registry;
    const uint64_t startUs = NowUs();
    const uint32_t probeId = registry.BeginScan(startUs);

    uint8_t probe[deskhub::kMaxDatagram];
    const size_t probeLen = deskhub::BuildDiscover(probe, probeId);
    if (!probeLen) return out;

    const uint64_t windowUs = uint64_t(windowMs) * 1000;
    const uint64_t gapUs = windowUs / kProbeBursts;
    uint64_t lastBurstUs = 0;
    int burstsSent = 0;

    uint8_t buf[deskhub::kMaxDatagram];
    for (;;) {
        const uint64_t now = NowUs();
        if (now - startUs >= windowUs) break;

        // Tới lượt phát: gửi vào broadcast của TỪNG card. Không kiểm tra kết quả gửi
        // — một card không gửi được (VPN vừa rớt) không được làm hỏng lượt quét của
        // các card còn lại.
        if (burstsSent < kProbeBursts && (burstsSent == 0 || now - lastBurstUs >= gapUs)) {
            for (const auto& c : casts)
                sock.SendTo(NetAddr{c.broadcastHost, hostPort}, probe, probeLen);
            lastBurstUs = now;
            ++burstsSent;
        }

        NetAddr from{};
        const int n = sock.RecvFrom(buf, sizeof(buf), from);
        if (n <= 0) continue; // 0 = hết timeout, quay lại kiểm tra lịch

        const auto span = std::span<const uint8_t>(buf, size_t(n));
        const auto h = deskhub::ParseCommonHeader(span);
        if (!h || h->type != deskhub::MsgType::Announce) continue;
        const auto ann = deskhub::ParseAnnounce(deskhub::PayloadOf(span));
        if (!ann) continue;

        // Chính máy này cũng nghe thấy broadcast của mình và trả lời. Hiện nó ra thì
        // người dùng bấm vào là tự kết nối tới bản thân.
        if (ann->hostId == LocalHostId()) continue;

        const uint64_t recvUs = NowUs();
        const uint32_t rttUs = uint32_t(recvUs - lastBurstUs);
        // Địa chỉ để hiển thị lấy IP NGUỒN của gói (đường thật sự tới được máy đó)
        // ghép với cổng host KHAI trong ANNOUNCE (cổng nguồn của gói trả lời là cổng
        // tạm, kết nối vào đó sẽ không có ai nghe).
        registry.OnAnnounce(*ann, FormatAddr(from.ip, ann->port ? ann->port : hostPort),
            rttUs, recvUs);
    }

    for (const auto& h : registry.hosts()) {
        DiscoveredMachine m;
        m.hostId = h.hostId;
        m.name = h.name.empty() ? h.address : h.name;
        m.address = h.address;
        m.rttMs = (h.rttUs + 500) / 1000; // làm tròn tới ms gần nhất
        m.sourceCount = h.sourceCount;
        m.acceptsInput = h.acceptsInput;
        m.busy = h.busy;
        out.push_back(std::move(m));
    }
    return out;
}
