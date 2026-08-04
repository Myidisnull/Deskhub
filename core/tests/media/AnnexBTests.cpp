#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/media/AnnexB.h"

#include <cstdio>
#include <vector>

using namespace deskhub::media;

namespace {

std::vector<uint8_t> Stream(std::initializer_list<std::vector<uint8_t>> nals, size_t scLen = 4) {
    std::vector<uint8_t> out;
    for (const auto& nal : nals) {
        if (scLen == 4) out.push_back(0);
        out.push_back(0);
        out.push_back(0);
        out.push_back(1);
        out.insert(out.end(), nal.begin(), nal.end());
    }
    return out;
}

void TestParse() {
    std::printf("[annexb] parsing splits a stream into typed NAL units...\n");
    const auto data = Stream({{0x67, 0xAA}, {0x68, 0xBB}, {0x65, 0x11, 0x22}});
    const auto nals = ParseAnnexB(data);
    Check(nals.size() == 3, "three NALs found");
    Check(H264NalType(nals[0].header) == 7 && H264NalType(nals[1].header) == 8 &&
              H264NalType(nals[2].header) == 5,
        "SPS, PPS, IDR in order");
    Check(nals[2].size == 3, "payload length excludes the start code");
    Check(nals[0].startCode == 4, "the start-code length is reported");

    const auto short3 = Stream({{0x41, 0x01}}, 3);
    const auto nals3 = ParseAnnexB(short3);
    Check(nals3.size() == 1 && nals3[0].startCode == 3, "3-byte start codes work too");

    Check(ParseAnnexB({}).empty(), "an empty buffer has no NALs");
    const std::vector<uint8_t> garbage{0x12, 0x34, 0x56};
    Check(ParseAnnexB(garbage).empty(), "garbage without start codes has none either");
}

void TestContainsIdr() {
    std::printf("[annexb] keyframe detection sees an IDR wherever it sits...\n");
    Check(ContainsIdr(Stream({{0x67}, {0x68}, {0x65, 0x00}})),
        "an IDR behind SPS/PPS is found");
    Check(!ContainsIdr(Stream({{0x41, 0x00}})), "a P-slice is not a keyframe");

    const auto tail = Stream({{0x41, 0x00}, {0x65}});
    Check(ContainsIdr(tail), "an IDR as the very last NAL is still found");
}

void TestFirstVcl() {
    std::printf("[annexb] the codec-config prefix ends where the first slice begins...\n");
    const std::vector<uint8_t> sps{0x67, 0xAA};
    const std::vector<uint8_t> pps{0x68, 0xBB};
    const std::vector<uint8_t> idr{0x65, 0x11};
    const auto data = Stream({sps, pps, idr});
    const size_t csd = FirstVclOffset(data);
    Check(csd == (4 + sps.size()) + (4 + pps.size()),
        "the offset covers exactly SPS+PPS with their start codes");
    Check(FirstVclOffset(Stream({sps, pps})) == 0, "no slice at all -> no prefix");
    Check(FirstVclOffset(Stream({idr})) == 0, "a bare slice has an empty prefix");
}

void TestAvccRoundTrip() {
    std::printf("[annexb] AVCC length prefixes convert to start codes and back...\n");
    const std::vector<uint8_t> nal{0x65, 0x01, 0x02, 0x03};
    std::vector<uint8_t> avcc;
    AppendLengthPrefixed(avcc, nal);
    Check(avcc.size() == 4 + nal.size(), "a 4-byte length is prepended");
    Check(avcc[3] == nal.size(), "and holds the payload length big-endian");

    const auto annexb = AvccToAnnexB(avcc, 4);
    Check(annexb == Stream({nal}), "converting back yields the Annex-B form");

    std::vector<uint8_t> truncated(avcc.begin(), avcc.end() - 2);
    const auto safe = AvccToAnnexB(truncated, 4);
    Check(safe.empty(), "a truncated buffer converts to nothing, not garbage");

    Check(AvccToAnnexB(avcc, 0).empty(), "a zero length size is rejected");
}

}

void RunAnnexBTests() {
    TestParse();
    TestContainsIdr();
    TestFirstVcl();
    TestAvccRoundTrip();
}
