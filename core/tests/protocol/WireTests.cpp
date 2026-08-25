#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/protocol/ByteOrder.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace deskhub;

namespace {

void TestWireRoundtrip() {
    std::printf("[wire] round-trip HELLO / HELLO_ACK / PING / REQUEST_KEYFRAME...\n");
    uint8_t buf[kMaxDatagram];

    Hello h{0xDEADBEEF, kCodecMaskH264, 2560, 1440, 120, 0x0001};
    h.passcode = "0417";
    size_t n = BuildHello(buf, h);
    Check(n == kCommonHeaderSize + 14 + kPasscodeDigits + 1, "HELLO size");
    auto ch = ParseCommonHeader(std::span<const uint8_t>(buf, n));
    Check(ch && ch->type == MsgType::Hello && ch->sessionId == 0, "HELLO header");
    auto hp = ParseHello(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(hp && hp->clientId == h.clientId && hp->codecMask == h.codecMask &&
              hp->maxWidth == h.maxWidth && hp->maxHeight == h.maxHeight &&
              hp->desiredFps == h.desiredFps && hp->features == h.features,
        "HELLO payload");
    Check(hp && hp->passcode == "0417", "HELLO carries the passcode with leading zero");
    Check(hp && hp->clientName.empty(), "HELLO without a name parses as empty");

    h.passcode.clear();
    n = BuildHello(buf, h);
    hp = ParseHello(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(hp && hp->passcode.empty(), "HELLO without a passcode parses as empty");

    h.passcode = "12ab";
    n = BuildHello(buf, h);
    hp = ParseHello(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(hp && hp->passcode.empty(), "an invalid passcode is never put on the wire");

    h.passcode = "0417";
    h.clientName =
        "Ph\xC3\xB2ng kh\xC3\xA1"
        "ch";
    n = BuildHello(buf, h);
    Check(n == kCommonHeaderSize + 14 + kPasscodeDigits + 1 + h.clientName.size(),
        "HELLO size grows by the name bytes");
    hp = ParseHello(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(hp && hp->clientName == h.clientName, "HELLO carries the UTF-8 client name");
    Check(hp && hp->passcode == "0417", "the passcode still parses in front of the name");

    hp = ParseHello(PayloadOf(std::span<const uint8_t>(buf, kCommonHeaderSize + 18)));
    Check(hp && hp->clientName.empty() && hp->passcode == "0417",
        "a legacy 18-byte HELLO parses with an empty name");

    std::string longName;
    while (longName.size() < kMaxClientNameBytes + 20) longName += "\xE1\xBA\xA1";
    h.clientName = longName;
    n = BuildHello(buf, h);
    hp = ParseHello(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(hp && hp->clientName.size() <= kMaxClientNameBytes,
        "a long name is truncated to the limit");
    Check(hp && hp->clientName.size() % 3 == 0, "truncation lands on a UTF-8 boundary");
    Check(hp && longName.compare(0, hp->clientName.size(), hp->clientName) == 0,
        "the truncated name is a prefix");

    h.clientName = "a\x01\x02\x7f b\ttab";
    n = BuildHello(buf, h);
    hp = ParseHello(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(hp && hp->clientName == "a btab", "control characters are stripped from the name");
    h.clientName.clear();

    Check(IsValidPasscode("0000") && IsValidPasscode("9999"), "all-digit passcodes are valid");
    Check(!IsValidPasscode("123") && !IsValidPasscode("12345") && !IsValidPasscode("12a4"),
        "wrong length or non-digits are invalid");

    Check(PasscodeFromRandom(0) == "0000" && PasscodeFromRandom(kPasscodeRange - 1) == "9999",
        "the generator spans the whole 4-digit range");
    Check(PasscodeFromRandom(417) == "0417", "a small draw keeps its leading zeros");
    Check(PasscodeFromRandom(kPasscodeRange) == "0000" &&
              PasscodeFromRandom(0xFFFFFFFFu) == "7295",
        "draws past the range wrap instead of overflowing");
    for (uint32_t seed = 0; seed < 5000; seed += 7)
        Check(IsValidPasscode(PasscodeFromRandom(seed * 2654435761u)),
            "every generated passcode passes validation");

    HelloAck a{0xCAFE0001, Codec::H264, 1920, 1080, 60, 20'000'000, 123'456'789'012ull};
    n = BuildHelloAck(buf, a);
    auto ap = ParseHelloAck(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(ap && ap->sessionId == a.sessionId && ap->codec == a.codec &&
              ap->width == a.width && ap->height == a.height && ap->fps == a.fps &&
              ap->bitrateBps == a.bitrateBps && ap->timebaseUs == a.timebaseUs,
        "HELLO_ACK payload");

    PingPong p{7, 999'999'999'999ull};
    n = BuildPing(buf, 0xCAFE0001, p);
    ch = ParseCommonHeader(std::span<const uint8_t>(buf, n));
    Check(ch && ch->type == MsgType::Ping && ch->sessionId == 0xCAFE0001, "PING header");
    auto pp = ParsePingPong(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(pp && pp->pingId == p.pingId && pp->sendTimeUs == p.sendTimeUs, "PING payload");

    n = BuildRequestKeyframe(buf, 0xCAFE0001);
    ch = ParseCommonHeader(std::span<const uint8_t>(buf, n));
    Check(ch && ch->type == MsgType::RequestKeyframe, "REQUEST_KEYFRAME");
}

void TestSourceListWire() {
    std::printf("[wire] SOURCE_LIST + HELLO.sourceId round-trip...\n");
    uint8_t buf[kMaxDatagram];

    const std::string kViet = "Man hinh \xE1\xBA\xA1\xE1\xBA\xA1";

    std::vector<SourceInfo> in;
    in.push_back(SourceInfo{0, 1920, 1080, "Display 1 (primary)"});
    in.push_back(SourceInfo{1, 2560, 1440, "Display 2"});
    in.push_back(SourceInfo{7, 800, 600, kViet});

    size_t n = BuildSourceList(buf, in);
    Check(n > 0 && n <= kMaxDatagram, "SOURCE_LIST fits one datagram");
    auto ch = ParseCommonHeader(std::span<const uint8_t>(buf, n));
    Check(ch && ch->type == MsgType::SourceList, "SOURCE_LIST header");

    SourceInfo out[kMaxSources];
    size_t cnt = ParseSourceList(PayloadOf(std::span<const uint8_t>(buf, n)), out);
    Check(cnt == in.size(), "SOURCE_LIST count");
    bool same = cnt == in.size();
    for (size_t i = 0; same && i < cnt; ++i)
        same = out[i].sourceId == in[i].sourceId && out[i].width == in[i].width &&
               out[i].height == in[i].height && out[i].name == in[i].name;
    Check(same, "SOURCE_LIST entries survive round-trip (including UTF-8 names)");

    n = BuildSourceList(buf, in, HostCaps{true, true, false});
    ch = ParseCommonHeader(std::span<const uint8_t>(buf, n));
    Check(ch && HostCapsOfFlags(ch->flags).acceptsInput && HostCapsOfFlags(ch->flags).terminal &&
              !HostCapsOfFlags(ch->flags).audio,
        "SOURCE_LIST carries what the host can do");
    n = BuildSourceList(buf, in, HostCaps{false, true, false});
    ch = ParseCommonHeader(std::span<const uint8_t>(buf, n));
    Check(ch && !HostCapsOfFlags(ch->flags).acceptsInput && HostCapsOfFlags(ch->flags).terminal &&
              !HostCapsOfFlags(ch->flags).audio,
        "a host that takes no input says so while still offering a terminal");
    n = BuildSourceList(buf, in, HostCaps{true, false, true});
    ch = ParseCommonHeader(std::span<const uint8_t>(buf, n));
    Check(ch && HostCapsOfFlags(ch->flags).acceptsInput && !HostCapsOfFlags(ch->flags).terminal &&
              HostCapsOfFlags(ch->flags).audio,
        "SOURCE_LIST carries audio when the host shares sound");
    n = BuildSourceList(buf, in);
    ch = ParseCommonHeader(std::span<const uint8_t>(buf, n));
    Check(ch && !HostCapsOfFlags(ch->flags).acceptsInput && !HostCapsOfFlags(ch->flags).terminal &&
              !HostCapsOfFlags(ch->flags).audio,
        "a host that says nothing promises nothing");

    std::vector<SourceInfo> longName;
    std::string vn;
    while (vn.size() < kMaxSourceNameBytes + 20) vn += "\xE1\xBA\xA1";
    longName.push_back(SourceInfo{3, 640, 480, vn});
    n = BuildSourceList(buf, longName);
    cnt = ParseSourceList(PayloadOf(std::span<const uint8_t>(buf, n)), out);
    Check(cnt == 1, "long-name SOURCE_LIST parses");
    if (cnt == 1) {
        Check(out[0].name.size() <= kMaxSourceNameBytes, "long name truncated to limit");
        Check(out[0].name.size() % 3 == 0, "truncation landed on a UTF-8 boundary");
        Check(vn.compare(0, out[0].name.size(), out[0].name) == 0, "truncated name is a prefix");
    }

    Hello h{0xDEADBEEF, kCodecMaskH264, 2560, 1440, 120, 0, 5};
    n = BuildHello(buf, h);
    auto hp = ParseHello(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(hp && hp->sourceId == 5, "HELLO carries sourceId");

    const uint8_t legacy13[13] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x0A, 0x00,
        0x05, 0xA0, 120, 0x00, 0x00};
    auto old = ParseHello(std::span<const uint8_t>(legacy13, sizeof(legacy13)));
    Check(old && old->sourceId == 0, "13-byte HELLO still parses as source 0");
}

void TestOversizedPacketsRejected() {
    std::printf("[wire] oversized video/FEC packets rejected at parse...\n");

    auto makeDatagram = [](MsgType type, size_t subHeader, size_t payloadBytes) {
        Datagram d(kCommonHeaderSize + subHeader + payloadBytes, 0);
        d[0] = kProtocolVersion;
        d[1] = uint8_t(type);
        d[2] = 0;
        d[3] = uint8_t(Chan::Video);
        return d;
    };

    {
        Datagram d = makeDatagram(MsgType::VideoPacket, kVideoHeaderSize, kMaxVideoPayload + 1);
        d[kCommonHeaderSize + 15] = 1;
        const auto h = ParseCommonHeader(d);
        Check(h.has_value(), "oversized video: common header still parses");
        if (h) Check(!ParseVideoPacket(*h, PayloadOf(d)).has_value(),
            "video payload > kMaxVideoPayload rejected");
    }
    {
        Datagram d = makeDatagram(MsgType::VideoPacket, kVideoHeaderSize, kMaxVideoPayload);
        d[kCommonHeaderSize + 15] = 1;
        const auto h = ParseCommonHeader(d);
        if (h) Check(ParseVideoPacket(*h, PayloadOf(d)).has_value(),
            "video payload == kMaxVideoPayload accepted");
    }
    {
        Datagram d = makeDatagram(MsgType::FecPacket, kFecHeaderSize,
            kFecLenPrefix + kMaxVideoPayload + 1);
        d[kCommonHeaderSize + 13] = 1;
        const auto h = ParseCommonHeader(d);
        Check(h.has_value(), "oversized FEC: common header still parses");
        if (h) Check(!ParseFecPacket(*h, PayloadOf(d)).has_value(),
            "FEC parity > kFecLenPrefix + kMaxVideoPayload rejected");
    }
}

void TestNackWire() {
    std::printf("[wire] NACK round-trip + error paths...\n");
    uint8_t buf[kMaxDatagram];

    const uint16_t idx[] = {0, 3, 4, 62, 1000};
    size_t n = BuildNack(buf, 0xCAFE0001, 0x11223344, idx);
    Check(n > 0, "BuildNack succeeded");
    const auto h = ParseCommonHeader(std::span<const uint8_t>(buf, n));
    Check(h && h->type == MsgType::Nack && h->sessionId == 0xCAFE0001, "NACK header");
    uint16_t out[16];
    uint32_t frameId = 0;
    size_t got = ParseNack(PayloadOf(std::span<const uint8_t>(buf, n)), frameId, out);
    Check(got == 5 && frameId == 0x11223344, "NACK count + frameId");
    bool same = true;
    for (size_t i = 0; i < got; ++i) same = same && out[i] == idx[i];
    Check(same, "NACK indices survive round-trip");

    Check(BuildNack(buf, 1, 0, std::span<const uint16_t>()) == 0, "empty NACK -> 0");
    std::vector<uint16_t> big(kMaxNackIndices + 1);
    Check(BuildNack(buf, 1, 0, big) == 0, "over-max NACK -> 0");
    uint8_t tiny[6];
    Check(BuildNack(tiny, 1, 0, idx) == 0, "NACK into too-small buffer -> 0");

    Check(ParseNack(std::span<const uint8_t>(buf, 4), frameId, out) == 0, "short NACK -> 0");
    {
        Datagram d(kNackHeaderSize + 4, 0);
        Check(ParseNack(d, frameId, out) == 0, "NACK count==0 -> 0");
        d[4] = 10;
        Check(ParseNack(d, frameId, out) == 0, "NACK count/payload mismatch -> 0");
    }
    uint16_t small[3];
    got = ParseNack(PayloadOf(std::span<const uint8_t>(buf, n)), frameId, small);
    Check(got == 3, "ParseNack clamps to out span size");
}

void TestInvalidateRefWire() {
    std::printf("[wire] INVALIDATE_REF round-trip + short payload...\n");
    uint8_t buf[kMaxDatagram];
    size_t n = BuildInvalidateRef(buf, 0xCAFE0001, 0xBEEF1234);
    const auto h = ParseCommonHeader(std::span<const uint8_t>(buf, n));
    Check(h && h->type == MsgType::InvalidateRef, "INVALIDATE_REF header");
    auto fid = ParseInvalidateRef(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(fid && *fid == 0xBEEF1234, "INVALIDATE_REF round-trip");
    Check(!ParseInvalidateRef(std::span<const uint8_t>(buf, 3)).has_value(), "short INVALIDATE_REF -> nullopt");
    uint8_t tiny[8];
    Check(BuildInvalidateRef(tiny, 1, 2) == 0, "INVALIDATE_REF into too-small buffer -> 0");
}

void TestClipboardWire() {
    std::printf("[wire] CLIPBOARD chunk round-trip + bounds...\n");
    uint8_t buf[kMaxDatagram];
    const std::string text = "clipboard payload";
    ClipboardChunkView chunk;
    chunk.revision = 0xAABBCCDD;
    chunk.chunkIndex = 1;
    chunk.chunkCount = 3;
    chunk.payload = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(text.data()), text.size());

    const size_t n = BuildClipboardChunk(buf, 0xCAFE0002, chunk);
    Check(n == kCommonHeaderSize + kClipboardHeaderSize + text.size(), "expected size");
    const auto h = ParseCommonHeader(std::span<const uint8_t>(buf, n));
    Check(h && h->type == MsgType::Clipboard && h->chan == Chan::Control,
        "CLIPBOARD header on the control channel");
    const auto v = ParseClipboardChunk(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(v && v->revision == chunk.revision && v->chunkIndex == 1 && v->chunkCount == 3,
        "CLIPBOARD fields round-trip");
    Check(v->payload.size() == text.size() &&
              std::memcmp(v->payload.data(), text.data(), text.size()) == 0,
        "CLIPBOARD payload intact");

    ClipboardChunkView bad = chunk;
    bad.chunkIndex = 3;
    Check(BuildClipboardChunk(buf, 1, bad) == 0, "index past count is refused");
    bad = chunk;
    bad.chunkCount = 0;
    Check(BuildClipboardChunk(buf, 1, bad) == 0, "zero chunk count is refused");
    bad = chunk;
    bad.chunkCount = uint16_t(kMaxClipboardChunks + 1);
    Check(BuildClipboardChunk(buf, 1, bad) == 0, "too many chunks are refused");

    Check(!ParseClipboardChunk(std::span<const uint8_t>(buf, 4)).has_value(),
        "short CLIPBOARD -> nullopt");
    uint8_t raw[kClipboardHeaderSize] = {};
    Check(!ParseClipboardChunk(raw).has_value(), "zero chunk count fails to parse");

    std::string over(kMaxClipboardTextBytes + 10, 'x');
    Check(TruncateClipboardText(over).size() == kMaxClipboardTextBytes,
        "oversize text is bounded");
}

void TestWireCoverage() {
    std::printf("[wire] remaining build/parse paths + control round-trips...\n");
    uint8_t buf[kMaxDatagram];
    uint8_t tiny[4];

    Check(BuildHello(tiny, Hello{}) == 0, "Build returns 0 when out too small");

    Check(!ParseCommonHeader(std::span<const uint8_t>(buf, 4)).has_value(), "short common header");
    {
        uint8_t bad[8] = {0x99};
        Check(!ParseCommonHeader(bad).has_value(), "wrong protocol version rejected");
    }
    Check(PayloadOf(std::span<const uint8_t>(buf, 4)).empty(), "PayloadOf on short datagram = empty");

    Check(!ParseHello(std::span<const uint8_t>(buf, 12)).has_value(), "short HELLO");
    Check(!ParseHelloAck(std::span<const uint8_t>(buf, 21)).has_value(), "short HELLO_ACK");
    Check(!ParsePingPong(std::span<const uint8_t>(buf, 11)).has_value(), "short PING");
    Check(!ParseFeedback(std::span<const uint8_t>(buf, 8)).has_value(), "short FEEDBACK");
    Check(!ParseReconfig(std::span<const uint8_t>(buf, 7)).has_value(), "short RECONFIG");
    Check(!ParseSetFocus(std::span<const uint8_t>(buf, 0)).has_value(), "empty SET_FOCUS");
    SourceInfo so[kMaxSources];
    Check(ParseSourceList(std::span<const uint8_t>(buf, 0), so) == 0,
        "empty SOURCE_LIST -> 0");

    size_t n = BuildFeedback(buf, 7, Feedback{10, 5, 33, 1234});
    auto fb = ParseFeedback(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(fb && fb->lostFrames == 10 && fb->lossPct == 5 && fb->rttMs == 33 &&
              fb->recvBitrateKbps == 1234,
        "FEEDBACK round-trip");

    n = BuildReconfig(buf, 7, Reconfig{1280, 720, 5'000'000, 30});
    auto rc = ParseReconfig(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(rc && rc->width == 1280 && rc->height == 720 && rc->bitrateBps == 5'000'000 &&
              rc->fps == 30,
        "RECONFIG round-trip (with fps)");

    {
        const uint8_t legacy[8] = {0x05, 0x00, 0x02, 0xD0, 0x00, 0x4C, 0x4B, 0x40};
        auto old = ParseReconfig(std::span<const uint8_t>(legacy, 8));
        Check(old && old->width == 0x0500 && old->height == 0x02D0 && old->fps == 0,
            "8-byte RECONFIG from an old host still parses, fps = 0 = unspecified");
    }

    n = BuildSetFocus(buf, 7, true);
    auto sf = ParseSetFocus(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(sf && *sf, "SET_FOCUS round-trip (true)");
    n = BuildSetFocus(buf, 7, false);
    sf = ParseSetFocus(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(sf && !*sf, "SET_FOCUS round-trip (false)");

    n = BuildPong(buf, 7, PingPong{9, 42});
    auto pg = ParsePingPong(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(pg && pg->pingId == 9 && pg->sendTimeUs == 42, "PONG round-trip");

    Check(BuildBye(buf, 7) > 0 && BuildStart(buf, 7) > 0 && BuildListSources(buf) > 0,
        "empty control messages build");

    uint8_t src[16] = {};
    {
        VideoHeader vh{};
        vh.frameId = 1;
        vh.pktIndex = 0;
        vh.pktCount = 0;
        n = BuildVideoPacket(buf, 7, vh, false, false, std::span<const uint8_t>(src, 10));
        const auto h = ParseCommonHeader(std::span<const uint8_t>(buf, n));
        Check(h && !ParseVideoPacket(*h, PayloadOf(std::span<const uint8_t>(buf, n))).has_value(),
            "video pktCount==0 rejected");
    }
    {
        VideoHeader vh{};
        vh.frameId = 1;
        vh.pktIndex = 5;
        vh.pktCount = 2;
        n = BuildVideoPacket(buf, 7, vh, false, false, std::span<const uint8_t>(src, 10));
        const auto h = ParseCommonHeader(std::span<const uint8_t>(buf, n));
        Check(h && !ParseVideoPacket(*h, PayloadOf(std::span<const uint8_t>(buf, n))).has_value(),
            "video pktIndex>=pktCount rejected");
    }

    Check(BuildInputEvents(buf, 7, 0, std::span<const InputEvent>()) == 0, "empty input batch -> 0");
    {
        std::vector<InputEvent> big(kMaxInputEvents + 1);
        Check(BuildInputEvents(buf, 7, 0, big) == 0, "over-max input batch -> 0");
    }
    {
        InputEvent one{};
        one.type = InputType::Key;
        n = BuildInputEvents(buf, 7, 5, std::span<const InputEvent>(&one, 1));
        const auto pl = PayloadOf(std::span<const uint8_t>(buf, n));
        std::vector<uint8_t> corrupt(pl.begin(), pl.end());
        InputEvent ev[4];
        uint32_t fs = 0;
        corrupt[4] = 0;
        Check(ParseInputEvents(corrupt, fs, ev) == 0, "input count==0 -> 0");
        corrupt[4] = 10;
        Check(ParseInputEvents(corrupt, fs, ev) == 0, "input count/payload mismatch -> 0");
    }
}

void TestAudioWire() {
    std::printf("[wire] AUDIO_PACKET build/parse round-trip...\n");
    uint8_t buf[kMaxDatagram];
    const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};

    AudioHeader ah{42, 1'234'567'890ull};
    const size_t n = BuildAudioPacket(buf, 0xCAFE0003, ah, std::span<const uint8_t>(payload));
    Check(n == kCommonHeaderSize + kAudioHeaderSize + sizeof(payload), "AUDIO_PACKET size");
    const auto h = ParseCommonHeader(std::span<const uint8_t>(buf, n));
    Check(h && h->type == MsgType::AudioPacket && h->chan == Chan::Audio &&
              h->sessionId == 0xCAFE0003,
        "AUDIO_PACKET header");
    const auto v = ParseAudioPacket(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(v && v->hdr.seq == ah.seq && v->hdr.timestampUs == ah.timestampUs &&
              v->payload.size() == sizeof(payload) &&
              std::memcmp(v->payload.data(), payload, sizeof(payload)) == 0,
        "AUDIO_PACKET payload");

    Check(BuildAudioPacket(buf, 1, ah, {}) == 0, "empty audio payload is refused");
    std::vector<uint8_t> huge(kMaxAudioPayload + 1, 0xAB);
    Check(BuildAudioPacket(buf, 1, ah, huge) == 0, "oversized audio payload is refused");
    Check(!ParseAudioPacket(std::span<const uint8_t>(buf, kAudioHeaderSize)).has_value(),
        "audio header-only payload fails to parse");
}

void TestFecWireErrors() {
    std::printf("[wire] FEC build/parse error paths...\n");
    uint8_t buf[kMaxDatagram];
    uint8_t parity[kFecLenPrefix + 100] = {};

    const FecHeader ok{1, 2, 10, 0};
    const size_t n = BuildFecPacket(buf, 7, ok, false, parity);
    Check(n > 0, "valid FEC packet builds");
    const auto h = ParseCommonHeader(std::span<const uint8_t>(buf, n));
    Check(h && ParseFecPacket(*h, PayloadOf(std::span<const uint8_t>(buf, n))).has_value(),
        "valid FEC packet parses");

    Check(h && !ParseFecPacket(*h, PayloadOf(std::span<const uint8_t>(buf, n)).first(kFecHeaderSize + 1))
                   .has_value(),
        "short FEC payload rejected");

    {
        const FecHeader bad{1, 2, 10, 5};
        const size_t m = BuildFecPacket(buf, 7, bad, false, parity);
        const auto hh = ParseCommonHeader(std::span<const uint8_t>(buf, m));
        Check(hh && !ParseFecPacket(*hh, PayloadOf(std::span<const uint8_t>(buf, m))).has_value(),
            "FEC groupIndex >= numGroups rejected");
    }
    {
        const FecHeader bad{1, 2, 0, 0};
        const size_t m = BuildFecPacket(buf, 7, bad, false, parity);
        const auto hh = ParseCommonHeader(std::span<const uint8_t>(buf, m));
        Check(hh && !ParseFecPacket(*hh, PayloadOf(std::span<const uint8_t>(buf, m))).has_value(),
            "FEC pktCount == 0 rejected");
    }
    const uint8_t tiny[1] = {};
    Check(BuildFecPacket(buf, 7, ok, false, std::span<const uint8_t>(tiny, 1)) == 0,
        "parity shorter than the length prefix -> 0");
}

void TestSourceListTruncation() {
    std::printf("[wire] SOURCE_LIST truncation + over-declared count...\n");
    uint8_t buf[kMaxDatagram];
    std::vector<SourceInfo> in;
    for (int i = 0; i < 12; ++i)
        in.push_back(SourceInfo{uint8_t(i), 100, 100, "S" + std::to_string(i)});
    const size_t n = BuildSourceList(buf, in);
    SourceInfo out[kMaxSources];
    const auto full = PayloadOf(std::span<const uint8_t>(buf, n));
    Check(ParseSourceList(full, out) == kMaxSources,
        "12 sources truncated to kMaxSources on build");

    Check(ParseSourceList(full.first(full.size() - 3), out) == kMaxSources - 1,
        "truncated tail record dropped, earlier ones kept");

    {
        Datagram d(1 + 6 + 2, 0);
        d[0] = 200;
        d[1] = 3;
        d[6] = 2;
        Check(ParseSourceList(d, out) == 1,
            "over-declared count clamped to what the payload holds");
    }

    {
        Datagram d{1, 4, 0x07, 0x80, 0x04, 0x38, 3, 'a', 'b', 'c'};
        Check(ParseSourceList(d, out) == 1, "a hand-built record parses");
        Check(out[0].sourceId == 4 && out[0].width == 0x0780 && out[0].height == 0x0438,
            "...with its fields in the right places");
        Check(out[0].name == "abc", "...and its name intact");
    }
}

void TestHelloAckReserved() {
    std::printf("[wire] HELLO_ACK: reserved bytes stay, reason keeps its offset...\n");
    uint8_t buf[kMaxDatagram];

    HelloAck a{};
    a.sessionId = 0x1234;
    a.codec = Codec::H264;
    a.width = 2560;
    a.height = 1600;
    a.fps = 60;
    a.bitrateBps = 20'000'000;
    a.timebaseUs = 0x1122334455667788ull;
    a.reason = RejectReason::Busy;
    const size_t n = BuildHelloAck(buf, a);
    const auto pl = PayloadOf(std::span<const uint8_t>(buf, n));
    Check(pl.size() == 25, "HELLO_ACK is still 25 bytes");
    Check(pl[22] == 0 && pl[23] == 0, "the reserved bytes go out as zero");
    Check(pl[24] == uint8_t(RejectReason::Busy), "reason still sits at offset 24");

    const auto got = ParseHelloAck(pl);
    Check(got && got->timebaseUs == a.timebaseUs, "the fields before the reserved bytes survive");
    Check(got && got->reason == RejectReason::Busy, "reason round-trip");

    const auto old = ParseHelloAck(pl.first(22));
    Check(old.has_value(), "a 22-byte HELLO_ACK still parses");
    Check(old && old->reason == RejectReason::None, "a host too old to say why is read as None");
}

void TestParseGarbage() {
    std::printf("[wire] 300 garbage buffers through every Parse*...\n");
    for (int i = 0; i < 300; ++i) {
        Datagram d(Rnd() % 1300, 0);
        for (auto& b : d) b = uint8_t(Rnd());
        const std::span<const uint8_t> s(d);
        const auto h = ParseCommonHeader(s);
        const auto pl = PayloadOf(s);
        ParseHello(pl);
        ParseHelloAck(pl);
        ParsePingPong(pl);
        ParseFeedback(pl);
        ParseReconfig(pl);
        ParseSetFocus(pl);
        ParseInvalidateRef(pl);
        SourceInfo so[kMaxSources];
        ParseSourceList(pl, so);
        uint32_t fid = 0;
        uint16_t idx[kMaxNackIndices];
        ParseNack(pl, fid, idx);
        InputEvent ev[kMaxInputEvents];
        uint32_t fs = 0;
        ParseInputEvents(pl, fs, ev);
        if (h) {
            ParseVideoPacket(*h, pl);
            ParseFecPacket(*h, pl);
        }
    }
    Check(true, "Parse* survived 300 garbage datagrams");
}

}

namespace {

void TestStateEventClassification() {
    std::printf("[wire] only keys and mouse buttons carry pressed state...\n");
    Check(IsStateEvent(InputType::Key), "a key press is a state event");
    Check(IsStateEvent(InputType::MouseButton), "a mouse button is a state event");
    Check(!IsStateEvent(InputType::MouseMove), "a mouse move is not");
    Check(!IsStateEvent(InputType::MouseWheel), "a wheel tick is not");
}

void TestRecordFraming() {
    std::printf("[wire] length-prefixed records cut a byte stream apart...\n");
    uint8_t one[kMaxDatagram];
    uint8_t two[kMaxDatagram];
    const size_t a = BuildPing(one, 7, PingPong{1, 2});
    const size_t b = BuildBye(two, 7);

    std::vector<uint8_t> stream(kMaxRecordSize);
    size_t used = BuildRecord(stream, std::span<const uint8_t>(one, a));
    Check(used == kRecordPrefixSize + a, "a record is its message plus the length prefix");
    used += BuildRecord(std::span<uint8_t>(stream).subspan(used),
        std::span<const uint8_t>(two, b));
    stream.resize(used);

    for (size_t cut = 0; cut < used; ++cut) {
        const RecordView partial = ReadRecord(std::span<const uint8_t>(stream).first(cut));
        if (cut < kRecordPrefixSize + a)
            Check(partial.status == RecordStatus::NeedMore,
                "a half-arrived record asks for more bytes");
        else
            Check(partial.status == RecordStatus::Ok, "a whole record is readable");
    }

    const RecordView first = ReadRecord(stream);
    Check(first.status == RecordStatus::Ok && first.consumed == kRecordPrefixSize + a,
        "the first record ends where the second begins");
    Check(first.message.size() == a &&
              std::memcmp(first.message.data(), one, a) == 0,
        "the framed message comes back byte for byte");
    const RecordView second = ReadRecord(
        std::span<const uint8_t>(stream).subspan(first.consumed));
    Check(second.status == RecordStatus::Ok && second.message.size() == b,
        "the second record follows immediately");

    const uint8_t zero[kRecordPrefixSize] = {0, 0};
    Check(ReadRecord(zero).status == RecordStatus::Invalid, "a zero-length record is junk");
    const uint8_t huge[kRecordPrefixSize] = {0xFF, 0xFF};
    Check(ReadRecord(huge).status == RecordStatus::Invalid,
        "a record larger than the cap is junk");

    uint8_t small[4];
    Check(BuildRecord(small, std::span<const uint8_t>(one, a)) == 0,
        "a record refuses to build into a buffer that cannot hold it");
    Check(BuildRecord(stream, std::span<const uint8_t>()) == 0, "an empty message is refused");
    const std::vector<uint8_t> over(kMaxRecordSize + 1, 0xAB);
    Check(BuildRecord(stream, over) == 0, "a message past the cap is refused");
}

void TestFileWire() {
    std::printf("[wire] file-transfer messages survive the round trip...\n");
    uint8_t buf[kMaxRecordSize];

    FileOffer offer;
    offer.batchId = 77;
    offer.files.push_back(TransferFile{0, "empty.bin"});
    offer.files.push_back(TransferFile{1234567, "\xe1\xba\xa2nh.png"});
    size_t n = BuildFileOffer(buf, offer);
    Check(n > 0, "an offer is built");
    auto header = ParseCommonHeader(std::span<const uint8_t>(buf, n));
    Check(header && header->chan == Chan::File && header->type == MsgType::FileOffer,
        "and rides the file channel");
    const auto backOffer = ParseFileOffer(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(backOffer && backOffer->batchId == 77 && backOffer->files.size() == 2,
        "the offer parses back");
    Check(backOffer->files[1].size == 1234567 && backOffer->files[1].name == "\xe1\xba\xa2nh.png",
        "with sizes and UTF-8 names intact");

    Check(BuildFileOffer(buf, FileOffer{1, {}}) == 0, "an empty batch is not built");
    Check(BuildFileOffer(buf, FileOffer{1, {TransferFile{4, "a/b"}}}) == 0,
        "a name carrying a separator is not built");
    Check(BuildFileOffer(buf, FileOffer{1, {TransferFile{kMaxTransferFileBytes + 1, "a"}}}) == 0,
        "a file past the size limit is not built");

    n = BuildFileAccept(buf, FileAccept{77, TransferReason::Busy});
    const auto accept = ParseFileAccept(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(accept && accept->batchId == 77 && accept->reason == TransferReason::Busy,
        "an accept carries its verdict");

    const std::vector<uint8_t> data(kMaxFileChunkBytes, 0x5A);
    n = BuildFileChunk(buf, 77, 3, 0xDEADBEEFull, data);
    Check(n > 0 && n <= kMaxRecordSize, "a full chunk fits one record exactly");
    const auto chunk = ParseFileChunk(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(chunk && chunk->batchId == 77 && chunk->fileIndex == 3 &&
              chunk->offset == 0xDEADBEEFull && chunk->data.size() == kMaxFileChunkBytes,
        "and parses back whole");
    Check(BuildFileChunk(buf, 1, 0, 0, {}) == 0, "an empty chunk is not built");
    Check(BuildFileChunk(buf, 1, uint16_t(kMaxTransferFiles), 0, data) == 0,
        "a chunk for a file index past the batch limit is not built");

    n = BuildFileDone(buf, FileDone{77, 3, 0xCBF43926u});
    const auto done = ParseFileDone(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(done && done->fileIndex == 3 && done->crc32 == 0xCBF43926u,
        "a done message carries the checksum");

    n = BuildFileAck(buf, FileAck{77, 3, TransferReason::Corrupt});
    const auto ack = ParseFileAck(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(ack && ack->fileIndex == 3 && ack->reason == TransferReason::Corrupt,
        "an ack carries its verdict");

    n = BuildFileCancel(buf, FileCancel{77, TransferReason::Cancelled});
    const auto cancel = ParseFileCancel(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(cancel && cancel->batchId == 77 && cancel->reason == TransferReason::Cancelled,
        "a cancel carries its reason");

    for (size_t len = 0; len < 24; ++len) {
        const std::span<const uint8_t> shortPayload(data.data(), len);
        if (len < 5) Check(!ParseFileAccept(shortPayload).has_value(),
            "a truncated accept is rejected");
        if (len < 10) Check(!ParseFileDone(shortPayload).has_value(),
            "a truncated done is rejected");
        if (len < 7) Check(!ParseFileAck(shortPayload).has_value(),
            "a truncated ack is rejected");
        if (len <= kFileChunkHeaderSize) Check(!ParseFileChunk(shortPayload).has_value(),
            "a chunk with no bytes in it is rejected");
        if (len < kFileOfferHeaderSize) Check(!ParseFileOffer(shortPayload).has_value(),
            "a truncated offer is rejected");
    }

    uint8_t bad[16] = {};
    PutU32(bad, 77);
    bad[4] = kMaxTransferReason + 1;
    Check(!ParseFileAccept(std::span<const uint8_t>(bad, 5)).has_value(),
        "an accept with an unknown reason is rejected");
    Check(!ParseFileCancel(std::span<const uint8_t>(bad, 5)).has_value(),
        "a cancel with an unknown reason is rejected");

    Check(HostCapFlags(HostCaps{false, false, false, true}) == kHostAcceptsFiles,
        "taking files has a capability flag of its own");
    Check(HostCapsOfFlags(kHostAcceptsFiles).files &&
              !HostCapsOfFlags(kHostAcceptsFiles).acceptsInput &&
              !HostCapsOfFlags(kHostAcceptsFiles).terminal &&
              !HostCapsOfFlags(kHostAcceptsFiles).audio,
        "and decodes without disturbing the others");
    Check(!HostCapsOfFlags(uint8_t(kHostAcceptsInput | kHostSharesAudio)).files,
        "a host from before file transfer decodes as taking none");
}

void TestPairingWire() {
    Fingerprint fp{};
    fp.bytes[0] = 0xAB;
    fp.bytes[31] = 0xCD;
    const PairingHello hello{fp, "laptop"};
    uint8_t buf[kMaxDatagram];
    const size_t n = BuildPairingHello(buf, hello);
    Check(n > 0, "pairing hello builds");
    const auto header = ParseCommonHeader(std::span<const uint8_t>(buf, n));
    Check(header && header->type == MsgType::PairingHello && header->chan == Chan::Control,
        "pairing hello header");
    const auto back = ParsePairingHello(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(back && back->fingerprint == fp && back->clientName == "laptop",
        "pairing hello round-trips");

    const PairingResult accepted{PairingResultCode::Accepted};
    const size_t rn = BuildPairingResult(buf, accepted);
    Check(rn > 0, "pairing result builds");
    const auto rback = ParsePairingResult(PayloadOf(std::span<const uint8_t>(buf, rn)));
    Check(rback && rback->code == PairingResultCode::Accepted, "pairing result round-trips");

    uint8_t truncated[kFingerprintBytes + 1]{};
    std::memcpy(truncated, fp.bytes.data(), kFingerprintBytes);
    truncated[kFingerprintBytes] = 4;
    Check(!ParsePairingHello(std::span<const uint8_t>(truncated, sizeof(truncated))).has_value(),
        "pairing hello rejects truncated name");
}

}

void RunWireTests() {
    TestWireRoundtrip();
    TestStateEventClassification();
    TestSourceListWire();
    TestOversizedPacketsRejected();
    TestNackWire();
    TestInvalidateRefWire();
    TestClipboardWire();
    TestAudioWire();
    TestWireCoverage();
    TestFecWireErrors();
    TestSourceListTruncation();
    TestHelloAckReserved();
    TestParseGarbage();
    TestRecordFraming();
    TestFileWire();
    TestPairingWire();
}
