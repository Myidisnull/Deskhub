// =============================================================================
// BeaconTests.cpp — hỏi-đáp trước phiên: Beacon trả lời LIST_SOURCES và PING dò.
//
// Trọng tâm không phải "gói dựng đúng byte chưa" (WireTests lo việc đó) mà là các
// LUẬT khiến câu trả lời dùng được: không có nguồn vẫn trả lời, ping trong phiên
// không phải việc của Beacon, và mọi gói thuộc phiên phải được để yên.
// =============================================================================
#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/session/Beacon.h"

#include <cstdio>

using namespace deskhub;

namespace {

// Cho Beacon một gói và trả về câu trả lời của nó (rỗng = không trả lời).
std::vector<uint8_t> Ask(const Beacon& b, std::span<const uint8_t> req) {
    uint8_t out[kMaxDatagram];
    const size_t n = b.Reply(out, req);
    return std::vector<uint8_t>(out, out + n);
}

void TestBeaconSourcesAndProbe() {
    std::printf("[disc] Beacon: LIST_SOURCES and the session-less PING probe...\n");
    Beacon b;
    SourceInfo s;
    s.sourceId = 3;
    s.width = 3840;
    s.height = 2160;
    s.name = "DELL U2723QE";
    b.SetSources(std::span<const SourceInfo>(&s, 1));

    uint8_t req[kMaxDatagram];
    size_t rn = BuildListSources(req);
    const auto rep = Ask(b, std::span<const uint8_t>(req, rn));
    const auto h = ParseCommonHeader(rep);
    Check(h && h->type == MsgType::SourceList, "LIST_SOURCES -> SOURCE_LIST");
    SourceInfo got[kMaxSources];
    Check(ParseSourceList(PayloadOf(rep), got) == 1, "one source listed");
    Check(got[0].sourceId == 3 && got[0].name == "DELL U2723QE",
        "the display entry survives the round trip");

    // Không có nguồn nào vẫn phải trả lời — im lặng bị client hiểu là host bản cũ.
    b.SetSources({});
    const auto rep2 = Ask(b, std::span<const uint8_t>(req, rn));
    const auto h2 = ParseCommonHeader(rep2);
    Check(h2 && h2->type == MsgType::SourceList, "a host sharing nothing still answers");
    Check(ParseSourceList(PayloadOf(rep2), got) == 0, "...with an empty list");

    // PING sessionId = 0: thăm dò một máy đã lưu, đo RTT mà không mở phiên.
    PingPong p{7, 123'456};
    rn = BuildPing(req, 0, p);
    const auto pong = Ask(b, std::span<const uint8_t>(req, rn));
    const auto ph = ParseCommonHeader(pong);
    Check(ph && ph->type == MsgType::Pong && ph->sessionId == 0, "PING sid=0 -> PONG sid=0");
    const auto pp = ParsePingPong(PayloadOf(pong));
    Check(pp && pp->pingId == 7 && pp->sendTimeUs == 123'456,
        "PONG echoes the payload verbatim so RTT is one subtraction");

    // PING TRONG phiên là việc của HostSession — nó phải nuôi timeout, Beacon không.
    rn = BuildPing(req, 42, p);
    Check(Ask(b, std::span<const uint8_t>(req, rn)).empty(),
        "in-session PING is not the Beacon's business");
}

void TestBeaconIgnoresSessionTraffic() {
    std::printf("[disc] Beacon: leaves session traffic alone...\n");
    const Beacon b;
    uint8_t req[kMaxDatagram];

    Hello hello{};
    hello.clientId = 5;
    hello.codecMask = kCodecMaskH264;
    size_t rn = BuildHello(req, hello);
    Check(Ask(b, std::span<const uint8_t>(req, rn)).empty(), "HELLO belongs to HostSession");

    rn = BuildRequestKeyframe(req, 9);
    Check(Ask(b, std::span<const uint8_t>(req, rn)).empty(), "REQUEST_KEYFRAME ignored");

    // Rác từ mạng: một cổng UDP mở thì ai cũng gửi tới được.
    const uint8_t junk[3] = {0xFF, 0x00, 0x7E};
    Check(Ask(b, junk).empty(), "a truncated/garbage datagram gets no reply");
    Check(Ask(b, {}).empty(), "an empty datagram gets no reply");
}

} // namespace

void RunBeaconTests() {
    TestBeaconSourcesAndProbe();
    TestBeaconIgnoresSessionTraffic();
}
