#pragma once
#include "deskhub/protocol/ByteOrder.h"
#include "deskhub/protocol/RecordStream.h"
#include "deskhub/protocol/Wire.h"
#include "deskhubp/net/UdpSocket.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

namespace deskhubp {

inline constexpr deskhub::MsgType kTermRecordType = deskhub::MsgType(0x5A);

inline bool SendTermMessage(UdpSocket& sock, const NetAddr& to,
    std::span<const uint8_t> message) {
    if (message.empty()) return false;
    if (message.size() + deskhub::kCommonHeaderSize <= deskhub::kMaxDatagram)
        return sock.SendTo(to, message.data(), message.size());

    std::vector<uint8_t> record(deskhub::kMaxRecordBacklog);
    const size_t total = deskhub::BuildRecord(record, message);
    if (!total) return false;

    size_t off = 0;
    while (off < total) {
        const size_t slice =
            std::min(deskhub::kMaxDatagram - deskhub::kCommonHeaderSize, total - off);
        uint8_t buf[deskhub::kMaxDatagram]{};
        buf[0] = deskhub::kProtocolVersion;
        buf[1] = uint8_t(kTermRecordType);
        buf[2] = 0;
        buf[3] = uint8_t(deskhub::Chan::Terminal);
        deskhub::PutU32(buf + 4, 0);
        std::memcpy(buf + deskhub::kCommonHeaderSize, record.data() + off, slice);
        const size_t n = deskhub::kCommonHeaderSize + slice;
        if (!sock.SendTo(to, buf, n)) return false;
        off += slice;
    }
    return true;
}

inline std::optional<std::vector<uint8_t>> FeedTermRecord(deskhub::RecordStream& stream,
    std::span<const uint8_t> datagram) {
    const auto header = deskhub::ParseCommonHeader(datagram);
    if (!header || header->chan != deskhub::Chan::Terminal) return std::nullopt;

    if (header->type == kTermRecordType) {
        stream.Append(deskhub::PayloadOf(datagram));
        std::vector<uint8_t> message;
        if (!stream.Next(message)) return std::nullopt;
        return message;
    }

    return std::vector<uint8_t>(datagram.begin(), datagram.end());
}

}
