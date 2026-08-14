#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/protocol/Wire.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace deskhub;

namespace {

using Builder = size_t (*)(std::span<uint8_t>);

struct Vector {
    const char* name;
    Builder build;
    const char* golden;
};

std::vector<uint8_t> BuiltOver(Builder build, uint8_t fill) {
    uint8_t buf[kMaxDatagram];
    std::memset(buf, fill, sizeof(buf));
    const size_t n = build(std::span<uint8_t>(buf, sizeof(buf)));
    return std::vector<uint8_t>(buf, buf + n);
}

std::vector<uint8_t> Built(Builder build) {
    return BuiltOver(build, 0x00);
}

Hello SampleHello() {
    Hello h{};
    h.clientId = 0x01020304;
    h.codecMask = kCodecMaskH264;
    h.maxWidth = 1920;
    h.maxHeight = 1080;
    h.desiredFps = 60;
    h.features = 0x0001;
    h.sourceId = 2;
    h.passcode = "7391";
    h.clientName = "Tablet 01";
    return h;
}

HelloAck SampleAck() {
    HelloAck a{};
    a.sessionId = 0x11223344;
    a.codec = Codec::H264;
    a.width = 1280;
    a.height = 720;
    a.fps = 30;
    a.bitrateBps = 8'000'000;
    a.timebaseUs = 0x0000000102030405ull;
    a.reason = RejectReason::None;
    return a;
}

std::vector<SourceInfo> SampleSources() {
    SourceInfo a;
    a.sourceId = 0;
    a.width = 1280;
    a.height = 720;
    a.name = "Display 1";

    SourceInfo b;
    b.sourceId = 1;
    b.width = 800;
    b.height = 600;
    b.name = "Display 2";
    return {a, b};
}

std::vector<InputEvent> SampleInput() {
    InputEvent key{};
    key.type = InputType::Key;
    key.timestampUs = 0x0102030405060708ull;
    key.a = 'A';
    key.b = 0x1E;
    key.state = 1;

    InputEvent move{};
    move.type = InputType::MouseMove;
    move.timestampUs = 0x0102030405060709ull;
    move.a = 32768;
    move.b = 16384;
    move.absolute = 1;

    InputEvent wheel{};
    wheel.type = InputType::MouseWheel;
    wheel.timestampUs = 0x010203040506070Aull;
    wheel.b = -120;
    return {key, move, wheel};
}

std::vector<Vector> AllVectors() {
    std::vector<Vector> v;

    v.push_back({"HELLO", [](std::span<uint8_t> out) {
                     return BuildHello(out, SampleHello());
                 },
        "0101000000000000010203040001078004383c00010237333931095461626c6574203031"});

    v.push_back({"HELLO_ACK", [](std::span<uint8_t> out) {
                     return BuildHelloAck(out, SampleAck());
                 },
        "01020000000000001122334400050002d01e007a12000000000102030405000000"});

    v.push_back({"START", [](std::span<uint8_t> out) {
                     return BuildStart(out, 0x11223344);
                 },
        "0103000011223344"});

    v.push_back({"BYE", [](std::span<uint8_t> out) {
                     return BuildBye(out, 0x11223344);
                 },
        "0104000011223344"});

    v.push_back({"LIST_SOURCES", [](std::span<uint8_t> out) {
                     return BuildListSources(out);
                 },
        "010500000000000000000000"});

    v.push_back({"SOURCE_LIST", [](std::span<uint8_t> out) {
                     const auto s = SampleSources();
                     return BuildSourceList(out, s);
                 },
        "01060000000000000200050002d009446973706c61792031010320025809446973706c61792032"});

    v.push_back({"PING", [](std::span<uint8_t> out) {
                     PingPong p{};
                     p.pingId = 7;
                     return BuildPing(out, 0x11223344, p);
                 },
        "0130000011223344000000070000000000000000"});

    v.push_back({"FEEDBACK", [](std::span<uint8_t> out) {
                     Feedback f{};
                     f.lostFrames = 12;
                     f.lossPct = 5;
                     f.rttMs = 40;
                     f.recvBitrateKbps = 6000;
                     return BuildFeedback(out, 0x11223344, f);
                 },
        "0132000011223344000c05002800001770"});

    v.push_back({"REQUEST_KEYFRAME", [](std::span<uint8_t> out) {
                     return BuildRequestKeyframe(out, 0x11223344);
                 },
        "0133000011223344"});

    v.push_back({"RECONFIG", [](std::span<uint8_t> out) {
                     Reconfig r{};
                     r.width = 960;
                     r.height = 540;
                     r.bitrateBps = 4'000'000;
                     return BuildReconfig(out, 0x11223344, r);
                 },
        "013400001122334403c0021c003d090000"});

    v.push_back({"SET_FOCUS", [](std::span<uint8_t> out) {
                     return BuildSetFocus(out, 0x11223344, true);
                 },
        "013500001122334401"});

    v.push_back({"NACK", [](std::span<uint8_t> out) {
                     const uint16_t idx[] = {1, 5, 9};
                     return BuildNack(out, 0x11223344, 0x0A0B0C0D, idx);
                 },
        "01360000112233440a0b0c0d03000100050009"});

    v.push_back({"INVALIDATE_REF", [](std::span<uint8_t> out) {
                     return BuildInvalidateRef(out, 0x11223344, 0x0A0B0C0D);
                 },
        "01370000112233440a0b0c0d"});

    v.push_back({"VIDEO_PACKET", [](std::span<uint8_t> out) {
                     VideoHeader vh{};
                     vh.frameId = 0x0A0B0C0D;
                     vh.timestampUs = 0x0102030405060708ull;
                     vh.pktIndex = 2;
                     vh.pktCount = 5;
                     const uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
                     return BuildVideoPacket(out, 0x11223344, vh, true, false, payload);
                 },
        "01100101112233440a0b0c0d010203040506070800020005deadbeef"});

    v.push_back({"FEC_PACKET", [](std::span<uint8_t> out) {
                     FecHeader fh{};
                     fh.frameId = 0x0A0B0C0D;
                     fh.timestampUs = 0x0102030405060708ull;
                     fh.pktCount = 5;
                     fh.groupIndex = 1;
                     const uint8_t parity[] = {0x01, 0x02, 0x03, 0x04};
                     return BuildFecPacket(out, 0x11223344, fh, true, parity);
                 },
        "01110101112233440a0b0c0d01020304050607080005010001020304"});

    v.push_back({"INPUT_EVENT", [](std::span<uint8_t> out) {
                     const auto e = SampleInput();
                     return BuildInputEvents(out, 0x11223344, 0x00010203, e);
                 },
        "01200002112233440001020303010102030405060708000000410000001e01000201020304050607090000800000004000000104010203040506070a00000000ffffff880000"});

    return v;
}

void TestEveryMessageMatchesItsGoldenBytes() {
    std::printf("[wire] every message type serialises to the same bytes on every OS...\n");
    for (const Vector& v : AllVectors()) {
        const std::vector<uint8_t> bytes = Built(v.build);
        Check(!bytes.empty(), "the builder produced a datagram");
        const std::string got = Hex(bytes);
        if (got == v.golden) continue;
        std::printf("  %-18s got %s\n", v.name, got.c_str());
        std::printf("  %-18s want %s\n", "", v.golden);
        Check(false, "the bytes on the wire changed — an older peer would misread them");
    }
}

void TestBuildersWriteEveryByteTheyClaim() {
    std::printf("[wire] a datagram never carries a byte the builder did not write...\n");
    for (const Vector& v : AllVectors()) {
        const std::vector<uint8_t> onZeros = BuiltOver(v.build, 0x00);
        const std::vector<uint8_t> onOnes = BuiltOver(v.build, 0xFF);
        if (onZeros == onOnes) continue;
        std::printf("  %-18s differs depending on what was already in the buffer\n", v.name);
        Check(false,
            "the length returned covers uninitialised memory — that leaks stack bytes onto "
            "the network and makes the message unreproducible");
    }
}

void TestNoMessageOutgrowsADatagram() {
    std::printf("[wire] no message we build can exceed one UDP datagram...\n");
    for (const Vector& v : AllVectors())
        Check(Built(v.build).size() <= kMaxDatagram, "every built message fits kMaxDatagram");
}

void TestEveryVectorParsesBackToWhatWentIn() {
    std::printf("[wire] each golden datagram still parses to the values it was built from...\n");

    uint8_t buf[kMaxDatagram];

    const size_t helloSize = BuildHello(buf, SampleHello());
    const auto helloHeader = ParseCommonHeader(std::span<const uint8_t>(buf, helloSize));
    Check(helloHeader && helloHeader->type == MsgType::Hello, "HELLO keeps its type byte");
    Check(helloHeader && helloHeader->ver == kProtocolVersion, "and the protocol version");
    const auto hello = ParseHello(PayloadOf(std::span<const uint8_t>(buf, helloSize)));
    Check(hello && hello->clientId == 0x01020304 && hello->maxWidth == 1920 &&
              hello->maxHeight == 1080 && hello->desiredFps == 60 && hello->sourceId == 2 &&
              hello->clientName == "Tablet 01",
        "every HELLO field survives the round trip");

    const size_t ackSize = BuildHelloAck(buf, SampleAck());
    const auto ack = ParseHelloAck(PayloadOf(std::span<const uint8_t>(buf, ackSize)));
    Check(ack && ack->sessionId == 0x11223344 && ack->width == 1280 && ack->height == 720 &&
              ack->fps == 30 && ack->bitrateBps == 8'000'000 &&
              ack->timebaseUs == 0x0000000102030405ull,
        "every HELLO_ACK field survives the round trip");

    const auto sources = SampleSources();
    const size_t listSize = BuildSourceList(buf, sources);
    SourceInfo back[kMaxSources];
    const size_t count = ParseSourceList(PayloadOf(std::span<const uint8_t>(buf, listSize)), back);
    Check(count == sources.size(), "the source list keeps its length");
    if (count == sources.size())
        for (size_t i = 0; i < count; ++i)
            Check(back[i].sourceId == sources[i].sourceId && back[i].width == sources[i].width &&
                      back[i].height == sources[i].height && back[i].name == sources[i].name,
                "and every row, including the UTF-8 name");

    const auto events = SampleInput();
    const size_t inputSize = BuildInputEvents(buf, 0x11223344, 0x00010203, events);
    uint32_t firstSeq = 0;
    InputEvent parsed[kMaxInputEvents];
    const size_t got =
        ParseInputEvents(PayloadOf(std::span<const uint8_t>(buf, inputSize)), firstSeq, parsed);
    Check(firstSeq == 0x00010203, "the input batch keeps its sequence number");
    Check(got == events.size(), "and every event in it");
    if (got == events.size())
        for (size_t i = 0; i < got; ++i)
            Check(parsed[i].type == events[i].type && parsed[i].a == events[i].a &&
                      parsed[i].b == events[i].b && parsed[i].state == events[i].state &&
                      parsed[i].absolute == events[i].absolute &&
                      parsed[i].timestampUs == events[i].timestampUs,
                "including negative deltas and the absolute flag");
}

}

void RunWireVectorTests() {
    TestBuildersWriteEveryByteTheyClaim();
    TestEveryMessageMatchesItsGoldenBytes();
    TestNoMessageOutgrowsADatagram();
    TestEveryVectorParsesBackToWhatWentIn();
}
