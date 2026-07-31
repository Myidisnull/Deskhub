#include "Tests.h"
#include "support/TestSupport.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace deskhub;

namespace {

void TestWireRoundtrip() {
    std::printf("[wire] round-trip HELLO / HELLO_ACK / PING / REQUEST_KEYFRAME...\n");
    uint8_t buf[kMaxDatagram];

    Hello h{0xDEADBEEF, kCodecMaskH264 | kCodecMaskHevc, 2560, 1440, 120, 0x0001};
    size_t n = BuildHello(buf, h);
    Check(n == kCommonHeaderSize + 14, "HELLO size");
    auto ch = ParseCommonHeader(std::span<const uint8_t>(buf, n));
    Check(ch && ch->type == MsgType::Hello && ch->sessionId == 0, "HELLO header");
    auto hp = ParseHello(PayloadOf(std::span<const uint8_t>(buf, n)));
    Check(hp && hp->clientId == h.clientId && hp->codecMask == h.codecMask &&
              hp->maxWidth == h.maxWidth && hp->maxHeight == h.maxHeight &&
              hp->desiredFps == h.desiredFps && hp->features == h.features,
        "HELLO payload");

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

void RunWireTests() {
    TestWireRoundtrip();
    TestSourceListWire();
    TestOversizedPacketsRejected();
    TestNackWire();
    TestInvalidateRefWire();
    TestWireCoverage();
    TestFecWireErrors();
    TestSourceListTruncation();
    TestHelloAckReserved();
    TestParseGarbage();
}
