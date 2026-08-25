#pragma once
#include "deskhub/protocol/ByteOrder.h"
#include "deskhub/protocol/RecordStream.h"
#include "deskhub/protocol/Wire.h"
#include "deskhubp/net/UdpSocket.h"

#include <cstdint>
#include <span>
#include <vector>

namespace deskhubp {

inline constexpr deskhub::MsgType kFileRecordType = deskhub::MsgType(0x76);

inline bool SendFileMessage(UdpSocket& sock, const NetAddr& to,
    std::span<const uint8_t> message) {
    if (message.empty()) return false;
    if (message.size() + deskhub::kCommonHeaderSize <= deskhub::kMaxDatagram)
        return sock.SendTo(to, message.data(), message.size()) > 0;

    std::vector<uint8_t> record(deskhub::kMaxRecordBacklog);
    const size_t total = deskhub::BuildRecord(record, message);
    if (!total) return false;

    size_t off = 0;
    while (off < total) {
        const size_t slice =
            std::min(deskhub::kMaxDatagram - deskhub::kCommonHeaderSize, total - off);
        uint8_t buf[deskhub::kMaxDatagram]{};
        buf[0] = deskhub::kProtocolVersion;
        buf[1] = uint8_t(kFileRecordType);
        buf[2] = 0;
        buf[3] = uint8_t(deskhub::Chan::File);
        deskhub::PutU32(buf + 4, 0);
        std::memcpy(buf + deskhub::kCommonHeaderSize, record.data() + off, slice);
        const size_t n = deskhub::kCommonHeaderSize + slice;
        if (sock.SendTo(to, buf, n) <= 0) return false;
        off += slice;
    }
    return true;
}

inline void FeedFileDatagram(deskhub::RecordStream& stream, std::span<const uint8_t> datagram,
    std::vector<std::vector<uint8_t>>& out) {
    const auto header = deskhub::ParseCommonHeader(datagram);
    if (!header || header->chan != deskhub::Chan::File) return;

    if (header->type == kFileRecordType) {
        const std::span<const uint8_t> payload = deskhub::PayloadOf(datagram);
        stream.Append(payload);
        for (;;) {
            std::vector<uint8_t> message;
            if (!stream.Next(message)) break;
            out.push_back(std::move(message));
        }
        return;
    }

    out.emplace_back(datagram.begin(), datagram.end());
}

}
