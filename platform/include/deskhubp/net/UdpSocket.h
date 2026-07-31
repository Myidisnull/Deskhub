#pragma once
#include <cstdint>
#include <string>

struct NetAddr {
    uint32_t ip = 0;
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

inline constexpr uint16_t kDeskhubPort = 47777;

bool ParseNetAddr(const std::string& s, NetAddr& out);

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    bool Open(uint16_t localPort);

    bool SetRecvTimeout(uint32_t ms);

    bool SendTo(const NetAddr& to, const uint8_t* data, size_t len);

    int RecvFrom(uint8_t* buf, size_t cap, NetAddr& from);

    void Close();

    bool IsOpen() const {
#ifdef _WIN32
        return sock_ != ~0ull;
#else
        return fd_ >= 0;
#endif
    }

    bool lastBindAddrInUse() const {
        return lastBindAddrInUse_;
    }

private:
#ifdef _WIN32
    uint64_t sock_ = ~0ull;
    bool wsaInit_ = false;
#else
    int fd_ = -1;
#endif
    bool lastBindAddrInUse_ = false;
};
