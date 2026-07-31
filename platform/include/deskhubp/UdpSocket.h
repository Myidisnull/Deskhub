#pragma once
// =============================================================================
// UdpSocket.h — bọc socket UDP, MỘT API cho cả năm nền tảng.
//
// NHIỆM VỤ
//   Che khác biệt giữa winsock2 và BSD socket sau một API duy nhất, để phần logic
//   phía trên (AgentLoop, ClientLoop, SourceQuery) đọc y hệt nhau ở mọi nền.
//
// ⚠ VÌ SAO GỘP (đổi 31/07/2026)
//   Trước đây có NĂM bản của file này. Khi so lại sau khi bỏ hết comment thì bốn
//   bản POSIX — iOS, macOS, Ubuntu, Android — khác nhau ĐÚNG BỐN DÒNG, và bốn dòng
//   đó chỉ là kiểu ép của timeval (`time_t/suseconds_t` so với `long`). Nghĩa là
//   suốt thời gian qua ta đang bảo trì bốn bản của cùng một file mà không đổi lại
//   được gì. Bản Windows thì khác thật (winsock2, WSAStartup, SIO_UDP_CONNRESET)
//   nên nó ở lại thành một .cpp riêng — nhưng CÙNG header này.
//
// VỊ TRÍ TRONG KIẾN TRÚC
//   core/ (deskhub) tuyệt đối không biết đến lớp này: nó chỉ nhận/giao byte qua
//   callback `send` và hàm HandlePacket. Toàn bộ hiểu biết về socket của app nằm ở
//   đây và ở người gọi trực tiếp nó. Đó cũng là lý do file này ở platform/ chứ
//   không phải core/: nó BUỘC phải include header hệ điều hành.
//
// QUY ƯỚC ĐỊA CHỈ
//   NetAddr giữ IP ở HOST byte order chứ không phải network byte order. Việc đổi
//   thứ tự byte dồn hết vào ranh giới gọi API hệ thống (htonl/ntohl trong .cpp),
//   nên mọi chỗ khác trong app so sánh và in địa chỉ một cách tự nhiên.
//
//   Pack()/Unpack() ép NetAddr vào một u64 để hai thread chia sẻ nó qua std::atomic
//   mà không cần khoá — AgentLoop dùng để cập nhật địa chỉ peer khi client roaming.
//
// SỞ HỮU TÀI NGUYÊN
//   Lớp này sở hữu handle của socket (và trên Windows là cả vòng đời
//   WSAStartup/WSACleanup): destructor tự dọn, và copy bị CẤM — hai đối tượng cùng
//   giữ một handle thì cái nào huỷ trước sẽ đóng socket của cái kia. Truyền đi thì
//   dùng tham chiếu, đừng truyền theo giá trị.
//
// LIÊN QUAN: platform/src/UdpSocketPosix.cpp, platform/src/UdpSocketWin.cpp
//            (hai bản cài đặt), deskhubp/SourceQuery.h, docs/06 §1.3
// =============================================================================
#include <cstdint>
#include <string>

// Địa chỉ IPv4 dạng host byte order — POD để so sánh/copy rẻ (roaming: peer đổi addr).
struct NetAddr {
    uint32_t ip = 0; // host byte order (127.0.0.1 = 0x7F000001)
    uint16_t port = 0;

    bool operator==(const NetAddr&) const = default;

    // Gói gọn vào u64 để chia sẻ giữa 2 thread bằng std::atomic (AgentLoop).
    uint64_t Pack() const {
        return (uint64_t(ip) << 16) | port;
    }
    static NetAddr Unpack(uint64_t v) {
        return NetAddr{uint32_t(v >> 16), uint16_t(v)};
    }
    std::string ToString() const;
};

// CỔNG CỐ ĐỊNH CỦA TOÀN BỘ SẢN PHẨM. Host luôn bind đúng cổng này, client luôn gửi
// tới đúng cổng này, và KHÔNG có đường nào đổi nó — không ô nhập, không cờ dòng
// lệnh, không tự nhảy sang cổng khác khi bận (chốt 2026-07-27).
//
// Vì sao không cho đổi: một cổng cố định biến "kết nối tới máy kia" thành đúng một
// thao tác — gõ IP. Cho đổi cổng thì mọi lời hướng dẫn, mọi nút Copy, mọi thông báo
// lỗi đều phải mang theo con số đó, và người dùng gặp thêm một cách để sai mà không
// đổi lại được gì trong mạng nhà. Cổng bận thì BÁO LỖI (xem lastBindAddrInUse) chứ
// không lặng lẽ nhảy cổng, vì nhảy cổng nghĩa là client gõ đúng IP vẫn không thấy máy.
inline constexpr uint16_t kDeskhubPort = 47777;

// "192.168.1.5" -> NetAddr với port = kDeskhubPort. false nếu sai cú pháp.
// CHỈ nhận IP trần: chuỗi có ':' bị TỪ CHỐI chứ không bỏ qua phần cổng, để người
// dùng dán "ip:port" kiểu cũ nhận được lời báo lỗi thay vì im lặng đi tới một cổng
// khác cái họ vừa gõ.
bool ParseNetAddr(const std::string& s, NetAddr& out);

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Mở + bind. `localPort` = 0 -> hệ thống cấp port ngẫu nhiên (phía client).
    // Trên Windows còn tự tắt WSAECONNRESET (ICMP port unreachable làm recvfrom
    // lỗi vĩnh viễn).
    bool Open(uint16_t localPort);

    // Timeout cho RecvFrom (ms). 0 = blocking vô hạn.
    bool SetRecvTimeout(uint32_t ms);

    bool SendTo(const NetAddr& to, const uint8_t* data, size_t len);

    // >0: số byte nhận; 0: timeout; <0: lỗi thật sự.
    int RecvFrom(uint8_t* buf, size_t cap, NetAddr& from);

    void Close();

    bool IsOpen() const {
#ifdef _WIN32
        return sock_ != ~0ull;
#else
        return fd_ >= 0;
#endif
    }

    // Sau khi Open() trả false: true nếu nguyên nhân là CỔNG ĐÃ BỊ CHIẾM —
    // trường hợp duy nhất người dùng xử lý được (đóng host cũ đang chạy). Che sau
    // bool để không lộ hằng của winsock/errno ra header.
    bool lastBindAddrInUse() const {
        return lastBindAddrInUse_;
    }

private:
#ifdef _WIN32
    // VÌ SAO sock_ LÀ uint64_t CHỨ KHÔNG PHẢI SOCKET
    //   Khai báo kiểu SOCKET trong header này sẽ kéo winsock2.h vào mọi file
    //   include nó, và winsock2.h xung khắc với windows.h nếu sai thứ tự (lỗi kinh
    //   điển: phải include winsock2.h TRƯỚC windows.h, không thì hàng trăm lỗi định
    //   nghĩa lại). Giấu nó sau uint64_t giữ header này sạch; ~0ull đóng vai
    //   INVALID_SOCKET.
    uint64_t sock_ = ~0ull;
    bool wsaInit_ = false;
#else
    int fd_ = -1;
#endif
    bool lastBindAddrInUse_ = false;
};
