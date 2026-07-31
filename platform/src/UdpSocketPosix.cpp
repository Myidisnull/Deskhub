// =============================================================================
// UdpSocketPosix.cpp — cài đặt bằng BSD socket. Dùng cho macOS, iOS, Ubuntu, Android.
//
// BỐ CỤC
//   NetAddr::ToString / ParseNetAddr — chuyển đổi địa chỉ ↔ chuỗi, không đụng socket.
//   Open / SetRecvTimeout / SendTo / RecvFrom / Close — vòng đời và I/O.
//
// QUY ƯỚC XỬ LÝ LỖI XUYÊN SUỐT
//   Hàm trả bool: true = thành công. Riêng RecvFrom trả int với BA vùng ý nghĩa —
//   >0 là số byte nhận được, 0 là HẾT TIMEOUT (chuyện bình thường, không phải lỗi),
//   <0 là lỗi thật khiến người gọi phải dừng vòng lặp. Phân biệt được 0 với lỗi là
//   điều kiện để vòng lặp mạng vừa nghe gói vừa chạy Tick theo nhịp đều đặn.
//
// VỀ THỨ TỰ BYTE
//   Đây là ranh giới duy nhất trong app có htonl/ntohl. NetAddr luôn giữ host byte
//   order; mọi lần chạm vào sockaddr_in đều kèm một phép đổi.
//
// BỐN NỀN POSIX DÙNG CHUNG ĐÚNG FILE NÀY
//   Trước 31/07/2026 đây là bốn file riêng, và chúng khác nhau đúng bốn dòng ép
//   kiểu timeval. Những khác biệt THẬT giữa bốn nền không nằm ở tầng socket mà ở
//   tầng app bundle: iOS và macOS 15+ đòi quyền Local Network
//   (NSLocalNetworkUsageDescription trong Info.plist) để gói UDP nội mạng ra vào
//   được, và iOS không bind được cổng cố định vì sandbox nên chỉ đóng vai client.
//   Không điều nào trong số đó là việc của file này.
//
// LIÊN QUAN: deskhubp/UdpSocket.h (API + lý do thiết kế),
//            platform/src/UdpSocketWin.cpp (bản winsock2)
// =============================================================================
#include "deskhubp/UdpSocket.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

#include "deskhubp/Log.h"

std::string NetAddr::ToString() const {
    char b[32];
    std::snprintf(b, sizeof(b), "%u.%u.%u.%u:%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
        (ip >> 8) & 0xFF, ip & 0xFF, port);
    return b;
}

// "192.168.1.5" -> NetAddr. Người dùng gõ chuỗi này vào ô địa chỉ trên UI nên nó là
// dữ liệu không tin được: mọi đường sai đều trả false để tầng trên báo lỗi tử tế,
// không có đường nào cho ra địa chỉ rác trông như hợp lệ.
// Chỉ IPv4, không phân giải tên miền — app này dùng trong mạng LAN.
bool ParseNetAddr(const std::string& s, NetAddr& out) {
    // Cổng là hằng số của sản phẩm (kDeskhubPort). Chuỗi có ':' bị từ chối thẳng:
    // im lặng cắt bỏ phần cổng sẽ khiến người dán "ip:50000" tưởng mình đã đổi cổng.
    if (s.find(':') != std::string::npos) return false;
    // inet_pton chứ không phải inet_addr: inet_addr trả về INADDR_NONE (0xFFFFFFFF)
    // khi lỗi, mà đó cũng là giá trị hợp lệ của 255.255.255.255 — không phân biệt
    // được. inet_pton trả về mã lỗi riêng nên chặt chẽ hơn.
    in_addr a{};
    if (inet_pton(AF_INET, s.c_str(), &a) != 1) return false;
    out.ip = ntohl(a.s_addr);
    out.port = kDeskhubPort;
    return true;
}

UdpSocket::~UdpSocket() {
    Close();
}

// Mở socket UDP và bind. Ghi vào fd_ CHỈ KHI mọi bước đã thành công — thất bại
// giữa chừng thì đóng fd cục bộ và để đối tượng nguyên trạng "chưa mở", nên gọi
// Open() thất bại rồi gọi lại là an toàn.
bool UdpSocket::Open(uint16_t localPort) {
    lastBindAddrInUse_ = false; // reset: chỉ nói về lần Open này

    const int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0) {
        LOGE("[UDP] socket() failed: %d", errno);
        return false;
    }

    // Không cần tắt SIO_UDP_CONNRESET như Windows: socket UDP chưa connect() trên
    // BSD/Darwin/Linux không nhận lỗi ICMP port-unreachable. RecvFrom vẫn nuốt
    // ECONNREFUSED cho chắc, phòng khi tầng dưới đẩy lên.

    // Nới buffer nhận của kernel lên 4 MB. Ở bitrate cao, một khoảng ngừng ngắn của
    // thread Net (bị hệ điều hành cho ra rìa, hoặc kẹt ở một vòng xử lý dài) là đủ
    // để buffer mặc định tràn và mất gói THẬT — thứ mất mát mà FEC lẫn việc xin IDR
    // đều không cứu nổi vì nó xảy ra trước khi gói đến tay chương trình.
    // Không kiểm tra trị trả về: kernel có thể kẹp xuống mức thấp hơn, và mức nào
    // cũng chạy được, chỉ là dễ mất gói hơn.
    int rcvbuf = 4 * 1024 * 1024;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

    // INADDR_ANY: nghe trên mọi giao diện mạng. Máy nào cũng có thể có nhiều đường
    // ra cùng lúc (Wi-Fi, Ethernet, di động, Tailscale) và ta không biết trước đầu
    // kia nằm ở nhánh nào.
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(localPort);
    if (bind(s, (sockaddr*)&local, sizeof(local)) != 0) {
        // EADDRINUSE là ca DUY NHẤT người dùng xử lý được (còn một host cũ đang
        // chạy) — tách ra để UI nói được câu đó thay vì "bind failed: 98".
        lastBindAddrInUse_ = (errno == EADDRINUSE);
        LOGE("[UDP] bind(:%u) failed: %d", localPort, errno);
        close(s);
        return false;
    }

    fd_ = s;
    return true;
}

// Đặt hạn chờ cho RecvFrom. Đây là thứ biến vòng lặp mạng từ "chặn vô hạn" thành
// "chặn tối đa N ms rồi trả 0" — nhờ vậy vòng lặp vẫn chạy Tick đều đặn (ping,
// timeout, phát lại) ngay cả khi host im lặng hoàn toàn.
bool UdpSocket::SetRecvTimeout(uint32_t ms) {
    if (!IsOpen()) return false;
    // Kiểu của hai trường timeval khác nhau giữa các hệ POSIX (tv_usec là int32 trên
    // Darwin, long trên Linux) — gán qua biến trung gian đúng kiểu của TRƯỜNG thay
    // vì ép cứng sang một kiểu nào đó, không thì một trong hai nền cảnh báo mất độ
    // chính xác. Đây đúng là bốn dòng từng làm bốn bản file này khác nhau.
    timeval tv{};
    tv.tv_sec = decltype(tv.tv_sec)(ms / 1000);
    tv.tv_usec = decltype(tv.tv_usec)((ms % 1000) * 1000);
    return setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
}

bool UdpSocket::SendTo(const NetAddr& to, const uint8_t* data, size_t len) {
    if (!IsOpen()) return false;
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(to.ip);
    sa.sin_port = htons(to.port);
    const ssize_t n = sendto(fd_, data, len, 0, (sockaddr*)&sa, sizeof(sa));
    // Đòi gửi TRỌN VẸN: với SOCK_DGRAM thì sendto hoặc gửi cả datagram hoặc không
    // gửi gì, nên gửi thiếu byte nghĩa là có chuyện bất thường. Không thử gửi lại —
    // ở tầng này mất gói là bình thường, các tầng trên đã có cơ chế phát lại riêng.
    return n == ssize_t(len);
}

// Nhận một datagram. Xem quy ước ba vùng giá trị trả về ở đầu file.
//
// Datagram dài hơn `cap` sẽ bị CẮT CỤT và phần dư mất luôn (đặc tính của UDP, không
// phải lỗi ở đây). Người gọi luôn truyền bộ đệm kMaxDatagram nên chuyện đó chỉ xảy
// ra khi có ai đó gửi gói không đúng giao thức, và gói cụt sẽ bị Wire.cpp loại.
int UdpSocket::RecvFrom(uint8_t* buf, size_t cap, NetAddr& from) {
    if (!IsOpen()) return -1;
    sockaddr_in sa{};
    socklen_t salen = sizeof(sa);
    const ssize_t n = recvfrom(fd_, buf, cap, 0, (sockaddr*)&sa, &salen);
    if (n >= 0) {
        from.ip = ntohl(sa.sin_addr.s_addr);
        from.port = ntohs(sa.sin_port);
        return int(n);
    }
    // Bốn mã lỗi này KHÔNG phải lỗi thật — quy về 0 để vòng lặp cứ chạy tiếp:
    //   EAGAIN/EWOULDBLOCK — hết hạn SO_RCVTIMEO, đúng như thiết kế.
    //   EINTR              — bị tín hiệu cắt ngang giữa chừng, thử lại là xong.
    //   ECONNREFUSED       — vọng lại từ ICMP port-unreachable khi host chưa mở
    //                        cổng. Bình thường trong lúc đang thử kết nối; coi là
    //                        lỗi thì app sẽ chết ngay lần thử đầu tiên.
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == ECONNREFUSED)
        return 0;
    return -1;
}

// Đặt lại fd_ = -1 sau khi đóng, nên gọi Close() nhiều lần là vô hại (destructor
// cũng gọi nó). Đóng hai lần một fd là lỗi nặng: số fd được cấp lại rất nhanh, lần
// đóng thứ hai có thể đóng nhầm socket hoặc file mà phần khác của app vừa mở.
void UdpSocket::Close() {
    if (IsOpen()) {
        close(fd_);
        fd_ = -1;
    }
}
