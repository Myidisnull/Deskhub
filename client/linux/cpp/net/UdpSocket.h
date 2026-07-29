#pragma once
// =============================================================================
// UdpSocket.h — bọc BSD socket, bản Ubuntu (CHÉP từ client/macos/.../net/UdpSocket.h).
//
// NHIỆM VỤ
//   Che khác biệt giữa API socket của các hệ điều hành sau MỘT API duy nhất, để
//   phần logic phía trên (ClientLoop, AgentLoop, SourceQuery) đọc y hệt nhau ở mọi
//   nền tảng. API ở đây GIỮ NGUYÊN từng chữ so với bản macOS/iOS/Android và bản
//   Windows — Linux cũng POSIX nên file này chép gần như nguyên (docs/11 §5:
//   "native mac/iOS/Linux dùng lại UdpSocket của Android").
//
// VỊ TRÍ TRONG KIẾN TRÚC
//   core/ (deskhub) tuyệt đối không biết đến lớp này: nó chỉ nhận/giao byte qua
//   callback `send` và hàm HandlePacket. Toàn bộ hiểu biết về socket của app nằm ở
//   đây và ở người gọi trực tiếp nó — trên Ubuntu là CẢ HAI VAI (ClientLoop và
//   AgentLoop).
//
// QUY ƯỚC ĐỊA CHỈ
//   NetAddr giữ IP ở HOST byte order chứ không phải network byte order. Việc đổi
//   thứ tự byte dồn hết vào ranh giới gọi API hệ thống (htonl/ntohl trong .cpp),
//   nên mọi chỗ khác trong app so sánh và in địa chỉ một cách tự nhiên.
//
// SỞ HỮU TÀI NGUYÊN
//   Lớp này sở hữu file descriptor: destructor tự đóng, và copy bị CẤM (nếu cho
//   copy thì hai đối tượng cùng giữ một fd và cái nào hủy trước sẽ đóng fd của cái
//   kia). Truyền đi thì dùng tham chiếu, đừng truyền theo giá trị.
//
// KHÁC BẢN macOS Ở ĐÂU
//   Chỉ MỘT dòng trong .cpp: Linux không có cờ SO_NOSIGPIPE, nhưng cũng không cần —
//   SIGPIPE chỉ phát sinh trên socket hướng dòng (TCP), không phải SOCK_DGRAM.
//   Phần còn lại giống hệt.
//
// LIÊN QUAN: client/macos/app/cpp/net/UdpSocket.h (bản song song, cùng API),
//            net/SourceQuery.h, ClientLoop.h, AgentLoop.h (người dùng)
// =============================================================================
#include <cstdint>
#include <string>

// Địa chỉ IPv4 dạng host byte order — POD, copy rẻ.
struct NetAddr {
    uint32_t ip = 0; // host byte order (127.0.0.1 = 0x7F000001)
    uint16_t port = 0;

    bool operator==(const NetAddr&) const = default;
    uint64_t Pack() const {
        return (uint64_t(ip) << 16) | port;
    }
    static NetAddr Unpack(uint64_t v) {
        return NetAddr{uint32_t(v >> 16), uint16_t(v)};
    }
    std::string ToString() const;
};

// CỔNG CỐ ĐỊNH CỦA TOÀN BỘ SẢN PHẨM — bản sao của hằng số cùng tên ở
// client/windows/cpp/net/UdpSocket.h (lý do đầy đủ nằm ở đó). Đổi thì đổi cả bốn
// nền tảng cùng lúc.
inline constexpr uint16_t kDeskhubPort = 47777;

// "192.168.1.5" -> NetAddr với port = kDeskhubPort. false nếu sai cú pháp.
// CHỈ nhận IP trần: chuỗi có ':' bị TỪ CHỐI chứ không bỏ qua phần cổng.
bool ParseNetAddr(const std::string& s, NetAddr& out);

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Mở + bind. `localPort` = 0 -> hệ thống cấp port ngẫu nhiên (phía client).
    bool Open(uint16_t localPort);

    // Timeout cho RecvFrom (ms). 0 = blocking vô hạn.
    bool SetRecvTimeout(uint32_t ms);

    bool SendTo(const NetAddr& to, const uint8_t* data, size_t len);

    // >0: số byte nhận; 0: timeout; <0: lỗi thật sự.
    int RecvFrom(uint8_t* buf, size_t cap, NetAddr& from);

    void Close();
    bool IsOpen() const {
        return fd_ >= 0;
    }

private:
    int fd_ = -1;
};
