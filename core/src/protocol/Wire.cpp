#include "deskhub/protocol/Wire.h"
#include "deskhub/protocol/ByteOrder.h"

#include <cstring>

namespace deskhub {

namespace {

size_t WriteCommon(std::span<uint8_t> out, MsgType type, uint8_t flags, Chan chan,
    uint32_t sessionId, size_t payloadSize) {
    const size_t total = kCommonHeaderSize + payloadSize;
    if (out.size() < total) return 0;
    out[0] = kProtocolVersion;
    out[1] = uint8_t(type);
    out[2] = flags;
    out[3] = uint8_t(chan);
    PutU32(out.data() + 4, sessionId);
    return total;
}

size_t BuildEmpty(std::span<uint8_t> out, MsgType type, uint32_t sessionId) {
    return WriteCommon(out, type, 0, Chan::Control, sessionId, 0);
}

size_t Utf8TruncLen(const std::string& s, size_t limit) {
    if (s.size() <= limit) return s.size();
    size_t n = limit;
    while (n > 0 && (uint8_t(s[n]) & 0xC0) == 0x80) --n;
    return n;
}

size_t BuildPingPongImpl(std::span<uint8_t> out, MsgType type, uint32_t sessionId,
    const PingPong& m) {
    constexpr size_t kPayload = 12;
    const size_t total = WriteCommon(out, type, 0, Chan::Control, sessionId, kPayload);
    if (!total) return 0;
    uint8_t* p = out.data() + kCommonHeaderSize;
    PutU32(p, m.pingId);
    PutU64(p + 4, m.sendTimeUs);
    return total;
}

}

size_t BuildHello(std::span<uint8_t> out, const Hello& m) {
    const size_t nameLen = Utf8TruncLen(m.clientName, kMaxClientNameBytes);
    const size_t kPayload = 14 + kPasscodeDigits + 1 + nameLen;
    const size_t total = WriteCommon(out, MsgType::Hello, 0, Chan::Control, 0, kPayload);
    if (!total) return 0;
    uint8_t* p = out.data() + kCommonHeaderSize;
    PutU32(p, m.clientId);
    PutU16(p + 4, m.codecMask);
    PutU16(p + 6, m.maxWidth);
    PutU16(p + 8, m.maxHeight);
    p[10] = m.desiredFps;
    PutU16(p + 11, m.features);
    p[13] = m.sourceId;
    const bool hasPasscode = IsValidPasscode(m.passcode);
    for (size_t i = 0; i < kPasscodeDigits; ++i)
        p[14 + i] = hasPasscode ? uint8_t(m.passcode[i]) : 0;
    p[14 + kPasscodeDigits] = uint8_t(nameLen);
    if (nameLen) std::memcpy(p + 14 + kPasscodeDigits + 1, m.clientName.data(), nameLen);
    return total;
}

size_t BuildListSources(std::span<uint8_t> out, std::string_view passcode) {
    const size_t total = WriteCommon(out, MsgType::ListSources, 0, Chan::Control, 0,
        kPasscodeDigits);
    if (!total) return 0;
    uint8_t* p = out.data() + kCommonHeaderSize;
    const bool hasPasscode = IsValidPasscode(passcode);
    for (size_t i = 0; i < kPasscodeDigits; ++i)
        p[i] = hasPasscode ? uint8_t(passcode[i]) : 0;
    return total;
}

size_t BuildSourceList(std::span<uint8_t> out, std::span<const SourceInfo> sources) {
    const size_t n = sources.size() < kMaxSources ? sources.size() : kMaxSources;
    size_t payload = 1;
    for (size_t i = 0; i < n; ++i) {
        payload += 6 + Utf8TruncLen(sources[i].name, kMaxSourceNameBytes);
    }
    const size_t total = WriteCommon(out, MsgType::SourceList, 0, Chan::Control, 0, payload);
    if (!total) return 0;

    uint8_t* p = out.data() + kCommonHeaderSize;
    *p++ = uint8_t(n);
    for (size_t i = 0; i < n; ++i) {
        const SourceInfo& s = sources[i];
        const size_t nameLen = Utf8TruncLen(s.name, kMaxSourceNameBytes);
        *p++ = s.sourceId;
        PutU16(p, s.width);
        p += 2;
        PutU16(p, s.height);
        p += 2;
        *p++ = uint8_t(nameLen);
        if (nameLen) std::memcpy(p, s.name.data(), nameLen);
        p += nameLen;
    }
    return total;
}

size_t BuildHelloAck(std::span<uint8_t> out, const HelloAck& m) {
    constexpr size_t kFixed = 24;
    const size_t total = WriteCommon(out, MsgType::HelloAck, 0, Chan::Control, 0, kFixed + 1);
    if (!total) return 0;
    uint8_t* p = out.data() + kCommonHeaderSize;
    PutU32(p, m.sessionId);
    p[4] = uint8_t(m.codec);
    PutU16(p + 5, m.width);
    PutU16(p + 7, m.height);
    p[9] = m.fps;
    PutU32(p + 10, m.bitrateBps);
    PutU64(p + 14, m.timebaseUs);
    PutU16(p + 22, 0);
    p[24] = uint8_t(m.reason);
    return total;
}

size_t BuildStart(std::span<uint8_t> out, uint32_t sessionId) {
    return BuildEmpty(out, MsgType::Start, sessionId);
}

size_t BuildBye(std::span<uint8_t> out, uint32_t sessionId) {
    return BuildEmpty(out, MsgType::Bye, sessionId);
}

size_t BuildPing(std::span<uint8_t> out, uint32_t sessionId, const PingPong& m) {
    return BuildPingPongImpl(out, MsgType::Ping, sessionId, m);
}

size_t BuildPong(std::span<uint8_t> out, uint32_t sessionId, const PingPong& m) {
    return BuildPingPongImpl(out, MsgType::Pong, sessionId, m);
}

size_t BuildFeedback(std::span<uint8_t> out, uint32_t sessionId, const Feedback& m) {
    constexpr size_t kPayload = 9;
    const size_t total = WriteCommon(out, MsgType::Feedback, 0, Chan::Control, sessionId, kPayload);
    if (!total) return 0;
    uint8_t* p = out.data() + kCommonHeaderSize;
    PutU16(p, m.lostFrames);
    p[2] = m.lossPct;
    PutU16(p + 3, m.rttMs);
    PutU32(p + 5, m.recvBitrateKbps);
    return total;
}

size_t BuildRequestKeyframe(std::span<uint8_t> out, uint32_t sessionId) {
    return BuildEmpty(out, MsgType::RequestKeyframe, sessionId);
}

size_t BuildSetFocus(std::span<uint8_t> out, uint32_t sessionId, bool focused) {
    const size_t total = WriteCommon(out, MsgType::SetFocus, 0, Chan::Control, sessionId, 1);
    if (!total) return 0;
    out[kCommonHeaderSize] = focused ? 1 : 0;
    return total;
}

size_t BuildNack(std::span<uint8_t> out, uint32_t sessionId, uint32_t frameId,
    std::span<const uint16_t> indices) {
    if (indices.empty() || indices.size() > kMaxNackIndices) return 0;
    const size_t payload = kNackHeaderSize + indices.size() * 2;
    const size_t total = WriteCommon(out, MsgType::Nack, 0, Chan::Control, sessionId, payload);
    if (!total) return 0;
    uint8_t* p = out.data() + kCommonHeaderSize;
    PutU32(p, frameId);
    p[4] = uint8_t(indices.size());
    uint8_t* q = p + kNackHeaderSize;
    for (uint16_t idx : indices) {
        PutU16(q, idx);
        q += 2;
    }
    return total;
}

size_t BuildInvalidateRef(std::span<uint8_t> out, uint32_t sessionId, uint32_t frameId) {
    const size_t total = WriteCommon(out, MsgType::InvalidateRef, 0, Chan::Control, sessionId, 4);
    if (!total) return 0;
    PutU32(out.data() + kCommonHeaderSize, frameId);
    return total;
}

size_t BuildReconfig(std::span<uint8_t> out, uint32_t sessionId, const Reconfig& m) {
    constexpr size_t kPayload = 9;
    const size_t total = WriteCommon(out, MsgType::Reconfig, 0, Chan::Control, sessionId, kPayload);
    if (!total) return 0;
    uint8_t* p = out.data() + kCommonHeaderSize;
    PutU16(p, m.width);
    PutU16(p + 2, m.height);
    PutU32(p + 4, m.bitrateBps);
    p[8] = m.fps;
    return total;
}

size_t BuildVideoPacket(std::span<uint8_t> out, uint32_t sessionId, const VideoHeader& vh,
    bool idr, bool frameEnd, std::span<const uint8_t> payload) {
    if (payload.size() > kMaxVideoPayload) return 0;
    const uint8_t flags = (idr ? kVideoFlagIdr : 0) | (frameEnd ? kVideoFlagFrameEnd : 0);
    const size_t total = WriteCommon(out, MsgType::VideoPacket, flags, Chan::Video, sessionId,
        kVideoHeaderSize + payload.size());
    if (!total) return 0;
    uint8_t* p = out.data() + kCommonHeaderSize;
    PutU32(p, vh.frameId);
    PutU64(p + 4, vh.timestampUs);
    PutU16(p + 12, vh.pktIndex);
    PutU16(p + 14, vh.pktCount);
    if (!payload.empty())
        std::memcpy(p + kVideoHeaderSize, payload.data(), payload.size());
    return total;
}

size_t BuildFecPacket(std::span<uint8_t> out, uint32_t sessionId, const FecHeader& fh,
    bool idr, std::span<const uint8_t> parity) {
    if (parity.size() < kFecLenPrefix ||
        parity.size() > kFecLenPrefix + kMaxVideoPayload) return 0;
    const uint8_t flags = idr ? kVideoFlagIdr : 0;
    const size_t total = WriteCommon(out, MsgType::FecPacket, flags, Chan::Video, sessionId,
        kFecHeaderSize + parity.size());
    if (!total) return 0;
    uint8_t* p = out.data() + kCommonHeaderSize;
    PutU32(p, fh.frameId);
    PutU64(p + 4, fh.timestampUs);
    PutU16(p + 12, fh.pktCount);
    p[14] = fh.groupIndex;
    p[15] = 0;
    std::memcpy(p + kFecHeaderSize, parity.data(), parity.size());
    return total;
}

size_t BuildInputEvents(std::span<uint8_t> out, uint32_t sessionId, uint32_t firstSeq,
    std::span<const InputEvent> events) {
    if (events.empty() || events.size() > kMaxInputEvents) return 0;
    const size_t payloadSize = kInputHeaderSize + events.size() * kInputEventSize;
    const size_t total = WriteCommon(out, MsgType::InputEvent, 0, Chan::Input, sessionId,
        payloadSize);
    if (!total) return 0;
    uint8_t* p = out.data() + kCommonHeaderSize;
    PutU32(p, firstSeq);
    p[4] = uint8_t(events.size());
    uint8_t* e = p + kInputHeaderSize;
    for (const auto& ev : events) {
        e[0] = uint8_t(ev.type);
        PutU64(e + 1, ev.timestampUs);
        PutU32(e + 9, uint32_t(ev.a));
        PutU32(e + 13, uint32_t(ev.b));
        e[17] = ev.state;
        e[18] = ev.absolute;
        e += kInputEventSize;
    }
    return total;
}

size_t BuildClipboardChunk(std::span<uint8_t> out, uint32_t sessionId,
    const ClipboardChunkView& chunk) {
    if (chunk.chunkCount == 0 || chunk.chunkIndex >= chunk.chunkCount) return 0;
    if (chunk.chunkCount > kMaxClipboardChunks) return 0;
    if (chunk.payload.size() > kMaxClipboardChunkPayload) return 0;
    const size_t payloadSize = kClipboardHeaderSize + chunk.payload.size();
    const size_t total = WriteCommon(out, MsgType::Clipboard, 0, Chan::Control, sessionId,
        payloadSize);
    if (!total) return 0;
    uint8_t* p = out.data() + kCommonHeaderSize;
    PutU32(p, chunk.revision);
    PutU16(p + 4, chunk.chunkIndex);
    PutU16(p + 6, chunk.chunkCount);
    if (!chunk.payload.empty())
        std::memcpy(p + kClipboardHeaderSize, chunk.payload.data(), chunk.payload.size());
    return total;
}

void WriteCommonHeader(std::span<uint8_t> out, const CommonHeader& h) {
    if (out.size() < kCommonHeaderSize) return;
    out[0] = h.ver ? h.ver : kProtocolVersion;
    out[1] = uint8_t(h.type);
    out[2] = h.flags;
    out[3] = uint8_t(h.chan);
    PutU32(out.data() + 4, h.sessionId);
}

size_t BuildNoise1(std::span<uint8_t> out, std::span<const uint8_t> body, uint8_t sourceId) {
    const size_t total =
        WriteCommon(out, MsgType::Noise1, 0, Chan::Control, sourceId, body.size());
    if (!total) return 0;
    if (!body.empty()) std::memcpy(out.data() + kCommonHeaderSize, body.data(), body.size());
    return total;
}

size_t BuildNoise2(std::span<uint8_t> out, std::span<const uint8_t> body) {
    const size_t total = WriteCommon(out, MsgType::Noise2, 0, Chan::Control, 0, body.size());
    if (!total) return 0;
    if (!body.empty()) std::memcpy(out.data() + kCommonHeaderSize, body.data(), body.size());
    return total;
}

size_t BuildNoise3(std::span<uint8_t> out, std::span<const uint8_t> body, uint8_t sourceId) {
    const size_t total =
        WriteCommon(out, MsgType::Noise3, 0, Chan::Control, sourceId, body.size());
    if (!total) return 0;
    if (!body.empty()) std::memcpy(out.data() + kCommonHeaderSize, body.data(), body.size());
    return total;
}

size_t BuildNoiseDecline(std::span<uint8_t> out) {
    return WriteCommon(out, MsgType::NoiseDecline, 0, Chan::Control, 0, 0);
}

std::optional<CommonHeader> ParseCommonHeader(std::span<const uint8_t> datagram) {
    if (datagram.size() < kCommonHeaderSize) return std::nullopt;
    if (datagram[0] != kProtocolVersion) return std::nullopt;
    CommonHeader h;
    h.ver = datagram[0];
    h.type = MsgType(datagram[1]);
    h.flags = datagram[2];
    h.chan = Chan(datagram[3]);
    h.sessionId = GetU32(datagram.data() + 4);
    return h;
}

std::span<const uint8_t> PayloadOf(std::span<const uint8_t> datagram) {
    if (datagram.size() < kCommonHeaderSize) return {};
    return datagram.subspan(kCommonHeaderSize);
}

std::optional<Hello> ParseHello(std::span<const uint8_t> payload) {
    if (payload.size() < 13) return std::nullopt;
    const uint8_t* p = payload.data();
    Hello m;
    m.clientId = GetU32(p);
    m.codecMask = GetU16(p + 4);
    m.maxWidth = GetU16(p + 6);
    m.maxHeight = GetU16(p + 8);
    m.desiredFps = p[10];
    m.features = GetU16(p + 11);
    m.sourceId = payload.size() >= 14 ? p[13] : 0;
    if (payload.size() >= 14 + kPasscodeDigits) {
        const std::string_view code(reinterpret_cast<const char*>(p + 14), kPasscodeDigits);
        if (IsValidPasscode(code)) m.passcode = code;
    }
    constexpr size_t nameLenOff = 14 + kPasscodeDigits;
    if (payload.size() > nameLenOff) {
        size_t nameLen = p[nameLenOff];
        if (nameLen > kMaxClientNameBytes) nameLen = 0;
        if (nameLen && payload.size() >= nameLenOff + 1 + nameLen) {
            m.clientName.reserve(nameLen);
            for (size_t i = 0; i < nameLen; ++i) {
                const uint8_t c = p[nameLenOff + 1 + i];
                if (c >= 0x20 && c != 0x7F) m.clientName.push_back(char(c));
            }
        }
    }
    return m;
}

std::string ParseListSourcesPasscode(std::span<const uint8_t> payload) {
    if (payload.size() < kPasscodeDigits) return {};
    const std::string_view code(reinterpret_cast<const char*>(payload.data()), kPasscodeDigits);
    return IsValidPasscode(code) ? std::string(code) : std::string();
}

size_t ParseSourceList(std::span<const uint8_t> payload, std::span<SourceInfo> out) {
    if (payload.empty()) return 0;
    constexpr size_t rec = 6;
    constexpr size_t lenOff = 5;

    size_t count = payload[0];
    if (count > out.size()) count = out.size();

    size_t off = 1;
    size_t written = 0;
    for (size_t i = 0; i < count; ++i) {
        if (off + rec > payload.size()) break;
        const uint8_t* p = payload.data() + off;
        const size_t nameLen = p[lenOff];
        if (off + rec + nameLen > payload.size()) break;
        SourceInfo s;
        s.sourceId = p[0];
        s.width = GetU16(p + 1);
        s.height = GetU16(p + 3);
        s.name.assign(reinterpret_cast<const char*>(p + rec), nameLen);
        out[written++] = std::move(s);
        off += rec + nameLen;
    }
    return written;
}

std::optional<HelloAck> ParseHelloAck(std::span<const uint8_t> payload) {
    if (payload.size() < 22) return std::nullopt;
    const uint8_t* p = payload.data();
    HelloAck m;
    m.sessionId = GetU32(p);
    if (p[4] != uint8_t(Codec::H264) && p[4] != uint8_t(Codec::Rejected)) return std::nullopt;
    m.codec = Codec(p[4]);
    m.width = GetU16(p + 5);
    m.height = GetU16(p + 7);
    m.fps = p[9];
    m.bitrateBps = GetU32(p + 10);
    m.timebaseUs = GetU64(p + 14);

    if (payload.size() >= 25 && p[24] <= uint8_t(RejectReason::WrongSessionKey))
        m.reason = RejectReason(p[24]);
    return m;
}

std::optional<PingPong> ParsePingPong(std::span<const uint8_t> payload) {
    if (payload.size() < 12) return std::nullopt;
    const uint8_t* p = payload.data();
    return PingPong{GetU32(p), GetU64(p + 4)};
}

std::optional<Feedback> ParseFeedback(std::span<const uint8_t> payload) {
    if (payload.size() < 9) return std::nullopt;
    const uint8_t* p = payload.data();
    Feedback m;
    m.lostFrames = GetU16(p);
    m.lossPct = p[2];
    m.rttMs = GetU16(p + 3);
    m.recvBitrateKbps = GetU32(p + 5);
    return m;
}

std::optional<Reconfig> ParseReconfig(std::span<const uint8_t> payload) {
    if (payload.size() < 8) return std::nullopt;
    const uint8_t* p = payload.data();
    const uint8_t fps = payload.size() >= 9 ? p[8] : 0;
    return Reconfig{GetU16(p), GetU16(p + 2), GetU32(p + 4), fps};
}

std::optional<bool> ParseSetFocus(std::span<const uint8_t> payload) {
    if (payload.empty()) return std::nullopt;
    return payload[0] != 0;
}

size_t ParseNack(std::span<const uint8_t> payload, uint32_t& frameId,
    std::span<uint16_t> out) {
    if (payload.size() < kNackHeaderSize) return 0;
    const uint8_t* p = payload.data();
    size_t count = p[4];
    if (count == 0) return 0;
    if (payload.size() < kNackHeaderSize + count * 2) return 0;
    if (count > out.size()) count = out.size();
    frameId = GetU32(p);
    const uint8_t* q = p + kNackHeaderSize;
    for (size_t i = 0; i < count; ++i) out[i] = GetU16(q + i * 2);
    return count;
}

std::optional<uint32_t> ParseInvalidateRef(std::span<const uint8_t> payload) {
    if (payload.size() < 4) return std::nullopt;
    return GetU32(payload.data());
}

std::optional<VideoPacketView> ParseVideoPacket(const CommonHeader& h,
    std::span<const uint8_t> payload) {
    if (payload.size() < kVideoHeaderSize) return std::nullopt;
    if (payload.size() > kVideoHeaderSize + kMaxVideoPayload) return std::nullopt;
    const uint8_t* p = payload.data();
    VideoPacketView v;
    v.hdr.frameId = GetU32(p);
    v.hdr.timestampUs = GetU64(p + 4);
    v.hdr.pktIndex = GetU16(p + 12);
    v.hdr.pktCount = GetU16(p + 14);
    v.idr = (h.flags & kVideoFlagIdr) != 0;
    v.frameEnd = (h.flags & kVideoFlagFrameEnd) != 0;
    v.payload = payload.subspan(kVideoHeaderSize);
    if (v.hdr.pktCount == 0 || v.hdr.pktIndex >= v.hdr.pktCount) return std::nullopt;
    return v;
}

std::optional<FecPacketView> ParseFecPacket(const CommonHeader& h,
    std::span<const uint8_t> payload) {
    if (payload.size() < kFecHeaderSize + kFecLenPrefix) return std::nullopt;
    if (payload.size() > kFecHeaderSize + kFecLenPrefix + kMaxVideoPayload)
        return std::nullopt;
    const uint8_t* p = payload.data();
    FecPacketView v;
    v.hdr.frameId = GetU32(p);
    v.hdr.timestampUs = GetU64(p + 4);
    v.hdr.pktCount = GetU16(p + 12);
    v.hdr.groupIndex = p[14];
    v.idr = (h.flags & kVideoFlagIdr) != 0;
    v.parity = payload.subspan(kFecHeaderSize);
    if (v.hdr.pktCount == 0) return std::nullopt;
    const size_t numGroups = (size_t(v.hdr.pktCount) + kFecGroupSize - 1) / kFecGroupSize;
    if (v.hdr.groupIndex >= numGroups) return std::nullopt;
    return v;
}

size_t ParseInputEvents(std::span<const uint8_t> payload, uint32_t& firstSeq,
    std::span<InputEvent> out) {
    if (payload.size() < kInputHeaderSize) return 0;
    const uint8_t* p = payload.data();
    const size_t count = p[4];
    if (count == 0 || count > out.size()) return 0;
    if (payload.size() < kInputHeaderSize + count * kInputEventSize) return 0;
    firstSeq = GetU32(p);
    const uint8_t* e = p + kInputHeaderSize;
    for (size_t i = 0; i < count; ++i) {
        InputEvent ev;
        ev.type = InputType(e[0]);
        ev.timestampUs = GetU64(e + 1);
        ev.a = int32_t(GetU32(e + 9));
        ev.b = int32_t(GetU32(e + 13));
        ev.state = e[17];
        ev.absolute = e[18];
        out[i] = ev;
        e += kInputEventSize;
    }
    return count;
}

std::optional<ClipboardChunkView> ParseClipboardChunk(std::span<const uint8_t> payload) {
    if (payload.size() < kClipboardHeaderSize) return std::nullopt;
    ClipboardChunkView v;
    const uint8_t* p = payload.data();
    v.revision = GetU32(p);
    v.chunkIndex = GetU16(p + 4);
    v.chunkCount = GetU16(p + 6);
    if (v.chunkCount == 0 || v.chunkCount > kMaxClipboardChunks) return std::nullopt;
    if (v.chunkIndex >= v.chunkCount) return std::nullopt;
    v.payload = payload.subspan(kClipboardHeaderSize);
    if (v.payload.size() > kMaxClipboardChunkPayload) return std::nullopt;
    return v;
}

std::string TruncateClipboardText(std::string_view text) {
    std::string out(text);
    out.resize(Utf8TruncLen(out, kMaxClipboardTextBytes));
    return out;
}

}
