// =============================================================================
// UdpSocketWin.cpp — cài đặt bằng winsock2. Bản Windows của deskhubp/UdpSocket.h.
//
// BỐ CỤC
//   NetAddr::ToString / ParseNetAddr — chuyển đổi địa chỉ ↔ chuỗi, không đụng socket.
//   Open / SetRecvTimeout / SendTo / RecvFrom / Close — vòng đời và I/O.
//
// QUY ƯỚC XỬ LÝ LỖI XUYÊN SUỐT
//   Hàm trả bool: true = thành công. Riêng RecvFrom trả int với BA vùng ý nghĩa —
//   >0 là số byte nhận được, 0 là HẾT TIMEOUT (chuyện bình thường, không phải lỗi),
//   <0 là lỗi thật khiến người gọi phải dừng vòng lặp.
//
// VÌ SAO ĐÂY LÀ FILE RIÊNG mà bốn nền POSIX dùng chung một file
//   Bốn bản POSIX khác nhau đúng bốn dòng ép kiểu; bản này khác THẬT — WSAStartup
//   có đếm tham chiếu, SIO_UDP_CONNRESET là cạm bẫy chỉ Windows có, SO_RCVTIMEO
//   nhận DWORD chứ không phải timeval, và mọi con trỏ đệm phải qua `char*`. Gộp
//   tiếp bằng #ifdef sẽ cho một file mà không nhánh nào đọc trôi.
//
// LIÊN QUAN: deskhubp/UdpSocket.h (API + lý do thiết kế),
//            platform/src/UdpSocketPosix.cpp (bản BSD socket), docs/06 §1.3
// =============================================================================
#include "deskhubp/UdpSocket.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdio>

#include "deskhubp/Log.h"

#pragma comment(lib, "ws2_32.lib")

// Một số SDK cũ không khai báo hằng này. Tự định nghĩa theo đúng công thức của
// Microsoft để build được trên mọi phiên bản SDK.
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

std::string NetAddr::ToString() const {
    char b[32];
    std::snprintf(b, sizeof(b), "%u.%u.%u.%u:%u",
        (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF, port);
    return b;
}

// "192.168.1.5" -> NetAddr. Người dùng gõ chuỗi này vào ô địa chỉ trên UI nên nó là
// dữ liệu không tin được: mọi đường sai đều trả false để tầng trên báo lỗi tử tế,
// không có đường nào cho ra địa chỉ rác trông như hợp lệ.
// Chỉ IPv4, không phân giải tên miền — chương trình này dùng trong mạng LAN.
bool ParseNetAddr(const std::string& s, NetAddr& out) {
    // Cổng là hằng số của sản phẩm (kDeskhubPort). Chuỗi có ':' bị từ chối thẳng:
    // im lặng cắt bỏ phần cổng sẽ khiến người dán "ip:50000" tưởng mình đã đổi cổng.
    if (s.find(':') != std::string::npos) return false;
    // InetPtonA chứ không phải inet_addr: inet_addr trả về INADDR_NONE (0xFFFFFFFF)
    // khi lỗi, mà đó cũng là giá trị hợp lệ của 255.255.255.255 — không phân biệt
    // được. InetPtonA trả về mã lỗi riêng nên chặt chẽ hơn.
    IN_ADDR a{};
    if (InetPtonA(AF_INET, s.c_str(), &a) != 1) return false;
    out.ip = ntohl(a.S_un.S_addr);
    out.port = kDeskhubPort;
    return true;
}

UdpSocket::~UdpSocket() {
    Close();
}

// Mở socket UDP và bind. Ghi vào sock_ CHỈ KHI mọi bước đã thành công — thất bại
// giữa chừng thì đóng handle cục bộ và để đối tượng nguyên trạng "chưa mở".
//
// WSAStartup đếm tham chiếu ở cấp tiến trình, nên gọi nhiều lần từ nhiều UdpSocket
// là hợp lệ; mỗi lần phải có đúng một WSACleanup đối ứng, và cờ wsaInit_ bảo đảm
// điều đó ngay cả khi Open() thất bại giữa chừng.
bool UdpSocket::Open(uint16_t localPort) {
    lastBindAddrInUse_ = false; // reset: chỉ nói về lần Open này
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        LOGE("[UDP] WSAStartup failed.");
        return false;
    }
    wsaInit_ = true;

    const SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) {
        LOGE("[UDP] socket() failed: %d", WSAGetLastError());
        return false;
    }

    // CẠM BẪY RIÊNG CỦA WINDOWS. Nếu một lần sendto trước đó gây ra ICMP "port
    // unreachable" (host chưa chạy, chuyện thường trong lúc client đang thử kết
    // nối), Windows sẽ nhớ điều đó và làm recvfrom trả WSAECONNRESET MÃI MÃI — kể
    // cả khi host đã lên và đang gửi dữ liệu bình thường. Socket coi như chết.
    //
    // Đây là hành vi Windows tự thêm, trái với tinh thần UDP: một giao thức không
    // kết nối thì không nên có khái niệm "phía kia từ chối". Linux không làm vậy,
    // nên bản Android không cần đoạn này.
    //
    // Tắt bằng WSAIoctl. Không kiểm tra trị trả về vì trên các bản Windows rất cũ
    // ioctl này có thể không tồn tại — lúc đó RecvFrom vẫn nuốt WSAECONNRESET như
    // lớp phòng thủ thứ hai.
    BOOL off = FALSE;
    DWORD bytes = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof(off), nullptr, 0, &bytes, nullptr, nullptr);

    // Nới buffer nhận của kernel lên 4 MB. Ở bitrate cao, một khoảng ngừng ngắn của
    // thread Net (bị hệ điều hành cho ra rìa, hoặc kẹt ở một vòng xử lý dài) là đủ
    // để buffer mặc định tràn và mất gói THẬT — thứ mất mát mà FEC lẫn việc xin IDR
    // đều không cứu nổi vì nó xảy ra trước khi gói đến tay chương trình.
    int rcvbuf = 4 * 1024 * 1024;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char*)&rcvbuf, sizeof(rcvbuf));

    // INADDR_ANY: nghe trên mọi giao diện mạng. Máy thường có nhiều đường ra cùng
    // lúc (Ethernet, Wi-Fi, vEthernet của Hyper-V/WSL) và ta không biết trước phía
    // kia sẽ tới từ nhánh nào — xem NetInfo.h về việc liệt kê chúng cho người dùng.
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(localPort);
    if (bind(s, (sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
        const int err = WSAGetLastError();
        // WSAEADDRINUSE là ca người dùng gặp thật: một host Deskhub cũ còn chạy
        // nền (cửa sổ menu bị ẩn suốt phiên share) vẫn giữ cổng. Tách riêng để tầng
        // trên báo cách xử lý thay vì phơi số lỗi 10048.
        lastBindAddrInUse_ = (err == WSAEADDRINUSE);
        if (lastBindAddrInUse_)
            LOGE(
                "[UDP] Port %u is already in use — another Deskhub host (or another "
                "program) is still listening on it.",
                localPort);
        else
            LOGE("[UDP] bind(:%u) failed: %d", localPort, err);
        closesocket(s);
        return false;
    }

    sock_ = uint64_t(s);
    return true;
}

// Đặt hạn chờ cho RecvFrom. Đây là thứ biến vòng lặp mạng từ "chặn vô hạn" thành
// "chặn tối đa N ms rồi trả 0" — nhờ vậy vòng lặp vẫn chạy Tick đều đặn (ping,
// timeout, phát lại) ngay cả khi phía kia im lặng hoàn toàn.
//
// Khác bản POSIX: winsock nhận DWORD mili-giây, không phải struct timeval.
bool UdpSocket::SetRecvTimeout(uint32_t ms) {
    if (!IsOpen()) return false;
    DWORD t = ms;
    return setsockopt(SOCKET(sock_), SOL_SOCKET, SO_RCVTIMEO,
               (const char*)&t, sizeof(t)) == 0;
}

bool UdpSocket::SendTo(const NetAddr& to, const uint8_t* data, size_t len) {
    if (!IsOpen()) return false;
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(to.ip);
    sa.sin_port = htons(to.port);
    // Đòi gửi TRỌN VẸN: với SOCK_DGRAM thì sendto hoặc gửi cả datagram hoặc không
    // gửi gì, nên gửi thiếu byte nghĩa là có chuyện bất thường. Không thử gửi lại —
    // ở tầng này mất gói là bình thường, các tầng trên đã có cơ chế phát lại riêng.
    return sendto(SOCKET(sock_), (const char*)data, int(len), 0,
               (sockaddr*)&sa, sizeof(sa)) == int(len);
}

// Nhận một datagram. Xem quy ước ba vùng giá trị trả về ở đầu file.
int UdpSocket::RecvFrom(uint8_t* buf, size_t cap, NetAddr& from) {
    if (!IsOpen()) return -1;
    sockaddr_in sa{};
    int salen = sizeof(sa);
    const int n = recvfrom(SOCKET(sock_), (char*)buf, int(cap), 0, (sockaddr*)&sa, &salen);
    if (n >= 0) {
        from.ip = ntohl(sa.sin_addr.s_addr);
        from.port = ntohs(sa.sin_port);
        return n;
    }
    // Ba mã lỗi này KHÔNG phải lỗi thật — quy về 0 để vòng lặp cứ chạy tiếp:
    //   WSAETIMEDOUT  — hết hạn SO_RCVTIMEO, đúng như thiết kế.
    //   WSAECONNRESET — vọng lại từ ICMP port-unreachable. Về lý thuyết đã bị tắt
    //                   bằng SIO_UDP_CONNRESET trong Open(), nhưng giữ ở đây làm
    //                   lớp phòng thủ thứ hai cho máy mà ioctl đó không có tác dụng.
    //   WSAEMSGSIZE   — datagram dài hơn `cap` nên bị cắt cụt. Người gọi luôn truyền
    //                   bộ đệm kMaxDatagram nên chỉ xảy ra khi ai đó gửi gói không
    //                   đúng giao thức; phần đã nhận được vẫn đưa lên và Wire.cpp
    //                   sẽ loại nó.
    const int err = WSAGetLastError();
    if (err == WSAETIMEDOUT || err == WSAECONNRESET || err == WSAEMSGSIZE) return 0;
    return -1;
}

// Đặt lại sock_ = ~0ull sau khi đóng, nên gọi Close() nhiều lần là vô hại
// (destructor cũng gọi nó). Đóng hai lần một handle là lỗi nặng: handle được cấp
// lại rất nhanh, lần đóng thứ hai có thể đóng nhầm socket mà phần khác vừa mở.
void UdpSocket::Close() {
    if (IsOpen()) {
        closesocket(SOCKET(sock_));
        sock_ = ~0ull;
    }
    if (wsaInit_) {
        WSACleanup();
        wsaInit_ = false;
    }
}
