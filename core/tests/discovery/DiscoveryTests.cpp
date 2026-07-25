// =============================================================================
// DiscoveryTests.cpp — hỏi-đáp trước phiên: Beacon trả lời gì, HostRegistry gom ra sao.
//
// Trọng tâm không phải "gói dựng đúng byte chưa" (WireTests lo việc đó) mà là các
// LUẬT khiến danh sách máy dùng được: gộp theo máy chứ không theo địa chỉ, thứ tự
// không nhảy, câu trả lời của lượt quét cũ bị bỏ, máy im lặng thì biến mất.
// =============================================================================
#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/discovery/Beacon.h"
#include "deskhub/discovery/HostRegistry.h"

#include <cstdio>
#include <string>

using namespace deskhub;

namespace {

// Dựng một Beacon "máy thật" để khỏi lặp bốn dòng ở mỗi ca.
Beacon MakeBeacon(bool acceptsInput = true, bool busy = false) {
    Beacon b;
    HostIdentity id;
    id.hostId = 0xABCD1234;
    id.port = 47777;
    id.name = "Studio Mac";
    b.SetIdentity(std::move(id));
    b.SetAcceptsInput(acceptsInput);
    b.SetBusy(busy);
    return b;
}

// Cho Beacon một gói và trả về câu trả lời của nó (rỗng = không trả lời).
std::vector<uint8_t> Ask(const Beacon& b, std::span<const uint8_t> req) {
    uint8_t out[kMaxDatagram];
    const size_t n = b.Reply(out, req);
    return std::vector<uint8_t>(out, out + n);
}

void TestBeaconDiscover() {
    std::printf("[disc] Beacon: DISCOVER -> ANNOUNCE, probeId dội lại...\n");
    Beacon b = MakeBeacon();
    SourceInfo s[2];
    s[0].sourceId = 0;
    s[0].name = "Xcode";
    s[1].sourceId = 1;
    s[1].kind = SourceKind::Display;
    s[1].name = "Built-in Retina Display";
    b.SetSources(std::span<const SourceInfo>(s, 2));

    uint8_t req[kMaxDatagram];
    const size_t rn = BuildDiscover(req, 0x11223344);
    const auto rep = Ask(b, std::span<const uint8_t>(req, rn));
    Check(!rep.empty(), "DISCOVER gets an answer");

    const auto h = ParseCommonHeader(rep);
    Check(h && h->type == MsgType::Announce, "the answer is an ANNOUNCE");
    const auto a = ParseAnnounce(PayloadOf(rep));
    Check(a.has_value(), "ANNOUNCE parses");
    Check(a->probeId == 0x11223344, "probeId echoed back so stale replies are droppable");
    Check(a->hostId == 0xABCD1234 && a->port == 47777, "identity carried");
    Check(a->name == "Studio Mac", "machine name carried");
    Check(a->sourceCount == 2, "source count carried");
    Check((a->flags & kAnnounceFlagAcceptsInput) != 0, "accepts-input advertised");
    Check((a->flags & kAnnounceFlagBusy) == 0, "idle host is not busy");

    b.SetBusy(true);
    b.SetAcceptsInput(false);
    const auto rep2 = Ask(b, std::span<const uint8_t>(req, rn));
    const auto a2 = ParseAnnounce(PayloadOf(rep2));
    Check(a2 && (a2->flags & kAnnounceFlagBusy) != 0, "busy host says so before you connect");
    Check(a2 && (a2->flags & kAnnounceFlagAcceptsInput) == 0, "view-only host says so too");
}

void TestBeaconSourcesAndProbe() {
    std::printf("[disc] Beacon: LIST_SOURCES and the session-less PING probe...\n");
    Beacon b = MakeBeacon();
    SourceInfo s;
    s.sourceId = 3;
    s.width = 3840;
    s.height = 2160;
    s.kind = SourceKind::Display;
    s.name = "DELL U2723QE";
    b.SetSources(std::span<const SourceInfo>(&s, 1));

    uint8_t req[kMaxDatagram];
    size_t rn = BuildListSources(req);
    const auto rep = Ask(b, std::span<const uint8_t>(req, rn));
    const auto h = ParseCommonHeader(rep);
    Check(h && h->type == MsgType::SourceList, "LIST_SOURCES -> SOURCE_LIST");
    SourceInfo got[kMaxSources];
    Check(ParseSourceList(*h, PayloadOf(rep), got) == 1, "one source listed");
    Check(got[0].kind == SourceKind::Display, "display vs window survives the round trip");

    // Không có nguồn nào vẫn phải trả lời — im lặng bị client hiểu là host bản cũ.
    b.SetSources({});
    const auto rep2 = Ask(b, std::span<const uint8_t>(req, rn));
    const auto h2 = ParseCommonHeader(rep2);
    Check(h2 && h2->type == MsgType::SourceList, "a host sharing nothing still answers");
    Check(ParseSourceList(*h2, PayloadOf(rep2), got) == 0, "...with an empty list");

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
    const Beacon b = MakeBeacon();
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

// Dựng ANNOUNCE cho các ca của HostRegistry.
HostAnnounce Ann(uint32_t probeId, uint32_t hostId, const char* name, uint8_t flags = 0) {
    HostAnnounce a;
    a.probeId = probeId;
    a.hostId = hostId;
    a.port = 47777;
    a.flags = flags;
    a.sourceCount = 1;
    a.name = name;
    return a;
}

void TestRegistryMergesAndSorts() {
    std::printf("[disc] HostRegistry: one row per machine, stable order...\n");
    HostRegistry r;
    const uint32_t probe = r.BeginScan(1'000'000);

    Check(r.OnAnnounce(Ann(probe, 2, "Windows rig"), "192.168.1.9:47777", 11'000, 1'010'000),
        "first sighting is a change");
    Check(r.OnAnnounce(Ann(probe, 1, "Studio Mac"), "192.168.1.7:47777", 4'000, 1'012'000),
        "second machine is a change");
    Check(r.hosts().size() == 2, "two machines");
    // Sắp theo tên, không theo thứ tự gói về — danh sách không được đảo chỗ dưới
    // ngón tay người dùng.
    Check(r.hosts()[0].name == "Studio Mac", "sorted by name, not by arrival");

    // Cùng máy, card mạng thứ hai: một dòng, và giữ đường NHANH HƠN.
    Check(!r.OnAnnounce(Ann(probe, 1, "Studio Mac"), "10.0.0.7:47777", 9'000, 1'014'000),
        "same machine on another NIC is not a new row");
    Check(r.hosts().size() == 2, "still two machines, not three");
    Check(r.hosts()[0].address == "192.168.1.7:47777", "keeps the faster path");
    Check(r.OnAnnounce(Ann(probe, 1, "Studio Mac"), "10.0.0.7:47777", 1'000, 1'016'000) ||
              r.hosts()[0].address == "10.0.0.7:47777",
        "switches when the other path is faster");
    Check(r.hosts()[0].rttUs == 1'000, "and takes its RTT");
}

void TestRegistryDropsStaleScan() {
    std::printf("[disc] HostRegistry: replies to the previous scan are dropped...\n");
    HostRegistry r;
    const uint32_t first = r.BeginScan(1'000'000);
    Check(r.OnAnnounce(Ann(first, 1, "Studio Mac"), "192.168.1.7:47777", 4'000, 1'010'000),
        "answer to the current scan lands");

    const uint32_t second = r.BeginScan(5'000'000);
    Check(second != first, "each scan gets its own id");
    // Câu trả lời của lượt trước còn lết về: RTT tính từ mốc cũ sẽ là con số bịa,
    // mà đó chính là con số hiện trên thẻ máy.
    Check(!r.OnAnnounce(Ann(first, 9, "Ghost"), "192.168.1.99:47777", 4'000'000, 5'010'000),
        "a late answer from the previous scan is ignored");
    Check(r.hosts().size() == 1, "...and does not create a phantom machine");
}

void TestRegistryExpiry() {
    std::printf("[disc] HostRegistry: machines that go quiet disappear...\n");
    HostRegistry r(6'000'000);
    const uint32_t probe = r.BeginScan(1'000'000);
    r.OnAnnounce(Ann(probe, 1, "Studio Mac"), "192.168.1.7:47777", 4'000, 1'010'000);
    r.OnAnnounce(Ann(probe, 2, "Windows rig"), "192.168.1.9:47777", 11'000, 1'020'000);

    Check(r.ReachableCount(2'000'000) == 2, "both reachable right after the scan");
    Check(r.Expire(2'000'000) == 0, "nothing expires within the window");

    // Chỉ một máy trả lời lượt sau.
    const uint32_t p2 = r.BeginScan(8'000'000);
    r.OnAnnounce(Ann(p2, 1, "Studio Mac"), "192.168.1.7:47777", 4'000, 8'010'000);
    Check(r.ReachableCount(8'020'000) == 1, "the silent machine no longer counts");
    Check(r.Expire(8'020'000) == 1, "and gets dropped");
    Check(r.hosts().size() == 1 && r.hosts()[0].hostId == 1, "the live one stays");
}

void TestRegistryCapacityAndFlags() {
    std::printf("[disc] HostRegistry: capacity cap and advertised flags...\n");
    HostRegistry r;
    const uint32_t probe = r.BeginScan(1'000'000);
    r.OnAnnounce(Ann(probe, 1, "Host", kAnnounceFlagBusy), "1.1.1.1:47777", 5'000, 1'001'000);
    Check(r.hosts()[0].busy, "busy flag decoded");
    Check(!r.hosts()[0].acceptsInput, "a host that did not advertise input is view-only");

    for (uint32_t i = 2; i < 64; ++i) {
        std::string name = "Host" + std::to_string(i);
        r.OnAnnounce(Ann(probe, i, name.c_str(), kAnnounceFlagAcceptsInput),
            "1.1.1.1:47777", 5'000, 1'001'000);
    }
    Check(r.hosts().size() == kMaxDiscoveredHosts, "list is capped, not unbounded");
}

} // namespace

void RunDiscoveryTests() {
    TestBeaconDiscover();
    TestBeaconSourcesAndProbe();
    TestBeaconIgnoresSessionTraffic();
    TestRegistryMergesAndSorts();
    TestRegistryDropsStaleScan();
    TestRegistryExpiry();
    TestRegistryCapacityAndFlags();
}
