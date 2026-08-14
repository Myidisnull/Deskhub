#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/media/BitWriter.h"
#include "deskhub/media/H264Sps.h"

#include <algorithm>
#include <cstdio>
#include <span>
#include <vector>

using deskhub::media::AnnexBSpsWithZeroReorder;
using deskhub::media::BitWriter;

namespace {

constexpr size_t kStartCodeBytes = 4;
constexpr uint8_t kSpsHeaderByte = 0x67;

class Reader {
public:
    explicit Reader(std::vector<uint8_t> rbsp)
        : bytes_(std::move(rbsp)) {}

    uint32_t U(uint32_t bits) {
        uint32_t v = 0;
        for (uint32_t i = 0; i < bits; ++i) v = (v << 1) | Bit();
        return v;
    }

    uint32_t UE() {
        uint32_t leading = 0;
        while (!Exhausted() && Bit() == 0) ++leading;
        if (!leading) return 0;
        return ((1u << leading) | U(leading)) - 1;
    }

private:
    bool Exhausted() const {
        return pos_ >= bytes_.size() * 8;
    }

    uint32_t Bit() {
        if (Exhausted()) return 0;
        const uint32_t b = (bytes_[pos_ >> 3] >> (7 - (pos_ & 7))) & 1;
        ++pos_;
        return b;
    }

    std::vector<uint8_t> bytes_;
    size_t pos_ = 0;
};

std::vector<uint8_t> Unescape(std::span<const uint8_t> nal) {
    std::vector<uint8_t> out;
    uint32_t zeros = 0;
    for (const uint8_t b : nal) {
        if (zeros >= 2 && b == 0x03) {
            zeros = 0;
            continue;
        }
        out.push_back(b);
        zeros = b == 0 ? zeros + 1 : 0;
    }
    return out;
}

struct SpsFields {
    uint32_t profileIdc = 100;
    uint32_t levelIdc = 42;
    uint32_t maxNumRefFrames = 1;
    uint32_t widthInMbs = 80;
    uint32_t heightInMapUnits = 52;
    bool withVui = false;
    uint32_t trailingZeroBytes = 0;
};

std::vector<uint8_t> MakeSpsNal(const SpsFields& f) {
    BitWriter w;
    w.StartNal(3, 7);
    w.U(8, f.profileIdc);
    w.U(8, 0);
    w.U(8, f.levelIdc);
    w.UE(0);
    if (f.profileIdc == 100) {
        w.UE(1);
        w.UE(0);
        w.UE(0);
        w.U(1, 0);
        w.U(1, 0);
    }
    w.UE(0);
    w.UE(2);
    w.UE(f.maxNumRefFrames);
    w.U(1, 0);
    w.UE(f.widthInMbs - 1);
    w.UE(f.heightInMapUnits - 1);
    w.U(1, 1);
    w.U(1, 1);
    w.U(1, 0);
    w.U(1, f.withVui ? 1 : 0);
    if (f.withVui) {
        w.U(1, 0);
        w.U(1, 0);
        w.U(1, 0);
        w.U(1, 0);
        w.U(1, 0);
        w.U(1, 0);
        w.U(1, 0);
        w.U(1, 0);
        w.U(1, 0);
    }
    w.Trailing();

    std::vector<uint8_t> nal(w.bytes().begin() + kStartCodeBytes, w.bytes().end());
    for (uint32_t i = 0; i < f.trailingZeroBytes; ++i) nal.push_back(0);
    return nal;
}

struct ParsedVui {
    bool ok = false;
    uint32_t profileIdc = 0;
    uint32_t levelIdc = 0;
    uint32_t maxNumRefFrames = 0;
    uint32_t widthInMbs = 0;
    uint32_t heightInMapUnits = 0;
    bool vuiPresent = false;
    bool bitstreamRestriction = false;
    uint32_t maxNumReorderFrames = 0;
    uint32_t maxDecFrameBuffering = 0;
};

ParsedVui ParseAnnexBSps(const std::vector<uint8_t>& annexb) {
    ParsedVui p;
    if (annexb.size() < kStartCodeBytes + 2) return p;
    if (annexb[0] || annexb[1] || annexb[2] || annexb[3] != 1) return p;
    if (annexb[kStartCodeBytes] != kSpsHeaderByte) return p;

    Reader r(Unescape(std::span<const uint8_t>(annexb).subspan(kStartCodeBytes + 1)));
    p.profileIdc = r.U(8);
    r.U(8);
    p.levelIdc = r.U(8);
    r.UE();
    if (p.profileIdc == 100) {
        r.UE();
        r.UE();
        r.UE();
        r.U(1);
        r.U(1);
    }
    r.UE();
    r.UE();
    p.maxNumRefFrames = r.UE();
    r.U(1);
    p.widthInMbs = r.UE() + 1;
    p.heightInMapUnits = r.UE() + 1;
    r.U(1);
    r.U(1);
    r.U(1);
    p.vuiPresent = r.U(1) != 0;
    if (p.vuiPresent) {
        for (int i = 0; i < 8; ++i) r.U(1);
        p.bitstreamRestriction = r.U(1) != 0;
        if (p.bitstreamRestriction) {
            r.U(1);
            r.UE();
            r.UE();
            r.UE();
            r.UE();
            p.maxNumReorderFrames = r.UE();
            p.maxDecFrameBuffering = r.UE();
        }
    }
    p.ok = true;
    return p;
}

void TestVuiIsAppended() {
    std::printf("[h264sps] an SPS with no VUI gets one that forbids frame reordering...\n");
    SpsFields f;
    f.maxNumRefFrames = 3;
    const std::vector<uint8_t> sps = MakeSpsNal(f);

    const std::vector<uint8_t> patched = AnnexBSpsWithZeroReorder(sps);
    Check(!patched.empty(), "an SPS without a VUI is rewritten");

    const ParsedVui p = ParseAnnexBSps(patched);
    Check(p.ok, "the rewritten SPS is a well formed Annex-B SPS NAL");
    Check(p.vuiPresent, "vui_parameters_present_flag is now set");
    Check(p.bitstreamRestriction, "bitstream_restriction_flag is set");
    Check(p.maxNumReorderFrames == 0, "max_num_reorder_frames is 0 - decoders must not buffer");
    Check(p.maxDecFrameBuffering == f.maxNumRefFrames,
        "max_dec_frame_buffering still covers the reference frames the stream uses");
}

void TestPrefixSurvivesUntouched() {
    std::printf("[h264sps] everything before the VUI comes back bit for bit...\n");
    SpsFields f;
    f.profileIdc = 100;
    f.levelIdc = 51;
    f.maxNumRefFrames = 2;
    f.widthInMbs = 120;
    f.heightInMapUnits = 68;

    const ParsedVui p = ParseAnnexBSps(AnnexBSpsWithZeroReorder(MakeSpsNal(f)));
    Check(p.ok, "parses back");
    Check(p.profileIdc == f.profileIdc && p.levelIdc == f.levelIdc, "profile and level kept");
    Check(p.maxNumRefFrames == f.maxNumRefFrames, "max_num_ref_frames kept");
    Check(p.widthInMbs == f.widthInMbs && p.heightInMapUnits == f.heightInMapUnits,
        "picture size kept");
}

void TestExistingVuiIsLeftAlone() {
    std::printf("[h264sps] an SPS that already carries a VUI is not touched...\n");
    SpsFields f;
    f.withVui = true;
    Check(AnnexBSpsWithZeroReorder(MakeSpsNal(f)).empty(),
        "an encoder that signals its own VUI keeps it");
}

void TestJunkIsRejected() {
    std::printf("[h264sps] anything that is not a parsable SPS is refused...\n");
    Check(AnnexBSpsWithZeroReorder({}).empty(), "empty input");

    const std::vector<uint8_t> pps{0x68, 0xCE, 0x3C, 0x80};
    Check(AnnexBSpsWithZeroReorder(pps).empty(), "a PPS is not an SPS");

    const std::vector<uint8_t> oneByte{kSpsHeaderByte};
    Check(AnnexBSpsWithZeroReorder(oneByte).empty(), "a header byte alone");

    const std::vector<uint8_t> truncated{kSpsHeaderByte, 0x64, 0x00};
    Check(AnnexBSpsWithZeroReorder(truncated).empty(), "an SPS that runs out mid-field");

    std::vector<uint8_t> allOnes(24, 0xFF);
    allOnes[0] = kSpsHeaderByte;
    AnnexBSpsWithZeroReorder(allOnes);
    Check(true, "a garbage payload returns without reading out of bounds");

    const std::vector<uint8_t> hugeScalingDelta{0x27, 0x7A, 0xD2, 0xD2, 0xD2, 0xD2, 0xD2, 0xD2,
        0x2E, 0x7A, 0xD2, 0xD0, 0xD0, 0xF2, 0x00, 0x00, 0x00, 0x01, 0xFF, 0xFF, 0xFE, 0x2C, 0xFB};
    AnnexBSpsWithZeroReorder(hugeScalingDelta);
    Check(true, "a scaling-list delta near INT32_MAX is stepped over without overflowing");
}

void TestEmulationPreventionIsApplied() {
    std::printf("[h264sps] the rewritten SPS still escapes start-code lookalikes...\n");
    SpsFields f;
    f.maxNumRefFrames = 0;
    f.widthInMbs = 1;
    f.heightInMapUnits = 1;

    const std::vector<uint8_t> patched = AnnexBSpsWithZeroReorder(MakeSpsNal(f));
    Check(!patched.empty(), "rewritten");

    size_t zeros = 0;
    bool startCodeInside = false;
    for (size_t i = kStartCodeBytes; i < patched.size(); ++i) {
        if (zeros >= 2 && patched[i] <= 1) startCodeInside = true;
        zeros = patched[i] == 0 ? zeros + 1 : 0;
    }
    Check(!startCodeInside, "no accidental start code survives inside the payload");

    const ParsedVui p = ParseAnnexBSps(patched);
    Check(p.ok && p.maxNumReorderFrames == 0, "and it still unescapes to the same SPS");
}

void TestTrailingBytesAreIgnored() {
    std::printf("[h264sps] padding after the stop bit does not confuse the rewrite...\n");
    SpsFields f;
    f.trailingZeroBytes = 3;
    const ParsedVui p = ParseAnnexBSps(AnnexBSpsWithZeroReorder(MakeSpsNal(f)));
    Check(p.ok && p.vuiPresent && p.maxNumReorderFrames == 0,
        "a padded SPS is rewritten the same way");
}

std::vector<uint8_t> WithStartCode(const std::vector<uint8_t>& nal, size_t startCodeBytes) {
    std::vector<uint8_t> out(startCodeBytes, 0);
    out.back() = 1;
    out.insert(out.end(), nal.begin(), nal.end());
    return out;
}

void Append(std::vector<uint8_t>& out, const std::vector<uint8_t>& more) {
    out.insert(out.end(), more.begin(), more.end());
}

std::vector<uint8_t> SpsOf(const std::vector<uint8_t>& annexb) {
    for (size_t i = 0; i + 4 < annexb.size(); ++i) {
        if (annexb[i] || annexb[i + 1] || annexb[i + 2] != 1) continue;
        if ((annexb[i + 3] & 0x1F) != 7) continue;
        size_t end = i + 3;
        while (end + 2 < annexb.size() &&
               !(!annexb[end] && !annexb[end + 1] && annexb[end + 2] == 1))
            ++end;
        std::vector<uint8_t> withCode{0, 0, 0, 1};
        withCode.insert(withCode.end(), annexb.begin() + i + 3, annexb.begin() + end);
        return withCode;
    }
    return {};
}

void TestStreamSpsIsRewrittenInPlace() {
    std::printf("[h264sps] the SPS inside a whole access unit is swapped, the rest is kept...\n");
    const std::vector<uint8_t> pps{0x68, 0xCE, 0x3C, 0x80};
    const std::vector<uint8_t> slice{0x65, 0x88, 0x84, 0x00, 0x21, 0xFF};

    std::vector<uint8_t> stream;
    Append(stream, WithStartCode(MakeSpsNal(SpsFields{}), 4));
    Append(stream, WithStartCode(pps, 4));
    Append(stream, WithStartCode(slice, 3));

    const std::vector<uint8_t> patched =
        deskhub::media::AnnexBStreamWithZeroReorder(stream);
    Check(!patched.empty(), "a stream carrying a VUI-less SPS is rewritten");

    const ParsedVui p = ParseAnnexBSps(SpsOf(patched));
    Check(p.ok && p.maxNumReorderFrames == 0, "the SPS in the stream now forbids reordering");

    std::vector<uint8_t> tail;
    Append(tail, WithStartCode(pps, 4));
    Append(tail, WithStartCode(slice, 3));
    const bool tailKept = patched.size() >= tail.size() &&
                          std::equal(tail.begin(), tail.end(), patched.end() - tail.size());
    Check(tailKept, "the PPS and slice NALs come through byte for byte");
}

void TestStreamWithThreeByteStartCodes() {
    std::printf("[h264sps] three-byte start codes are recognised too...\n");
    std::vector<uint8_t> stream;
    Append(stream, WithStartCode(MakeSpsNal(SpsFields{}), 3));
    Append(stream, WithStartCode({0x68, 0xCE, 0x3C, 0x80}, 3));

    const ParsedVui p =
        ParseAnnexBSps(SpsOf(deskhub::media::AnnexBStreamWithZeroReorder(stream)));
    Check(p.ok && p.maxNumReorderFrames == 0, "rewritten through a 3-byte start code");
}

void TestStreamWithoutSpsIsLeftAlone() {
    std::printf("[h264sps] a stream the rewrite cannot improve is reported unchanged...\n");
    std::vector<uint8_t> noSps;
    Append(noSps, WithStartCode({0x68, 0xCE, 0x3C, 0x80}, 4));
    Append(noSps, WithStartCode({0x41, 0x9A, 0x00, 0x11}, 4));
    Check(deskhub::media::AnnexBStreamWithZeroReorder(noSps).empty(),
        "a stream with no SPS is not copied at all");

    SpsFields already;
    already.withVui = true;
    std::vector<uint8_t> good;
    Append(good, WithStartCode(MakeSpsNal(already), 4));
    Append(good, WithStartCode({0x68, 0xCE, 0x3C, 0x80}, 4));
    Check(deskhub::media::AnnexBStreamWithZeroReorder(good).empty(),
        "an encoder that already signals a VUI is left alone");

    Check(deskhub::media::AnnexBStreamWithZeroReorder({}).empty(), "an empty stream");

    const std::vector<uint8_t> noStartCode{0x67, 0x64, 0x00, 0x2A};
    Check(deskhub::media::AnnexBStreamWithZeroReorder(noStartCode).empty(),
        "bytes with no start code at all");
}

void FinishSpsTail(BitWriter& w, bool frameMbsOnly = true, bool withCrop = false) {
    w.UE(1);
    w.U(1, 0);
    w.UE(79);
    w.UE(51);
    w.U(1, frameMbsOnly ? 1 : 0);
    if (!frameMbsOnly) w.U(1, 0);
    w.U(1, 1);
    w.U(1, withCrop ? 1 : 0);
    if (withCrop) {
        w.UE(0);
        w.UE(4);
        w.UE(0);
        w.UE(2);
    }
    w.U(1, 0);
    w.Trailing();
}

std::vector<uint8_t> NalOf(const BitWriter& w) {
    return std::vector<uint8_t>(w.bytes().begin() + kStartCodeBytes, w.bytes().end());
}

std::vector<uint8_t> MakeHighProfileSps(uint32_t profileIdc, uint32_t chromaFormatIdc,
    bool withScalingLists) {
    BitWriter w;
    w.StartNal(3, 7);
    w.U(8, profileIdc);
    w.U(8, 0);
    w.U(8, 42);
    w.UE(0);
    w.UE(chromaFormatIdc);
    if (chromaFormatIdc == 3) w.U(1, 0);
    w.UE(0);
    w.UE(0);
    w.U(1, 0);
    w.U(1, withScalingLists ? 1 : 0);
    if (withScalingLists) {
        const uint32_t lists = chromaFormatIdc == 3 ? 12 : 8;
        for (uint32_t i = 0; i < lists; ++i) {
            if (i == 0) {
                w.U(1, 1);
                for (int e = 0; e < 16; ++e) w.SE(1);
            } else if (i == 6) {
                w.U(1, 1);
                w.SE(-8);
            } else {
                w.U(1, 0);
            }
        }
    }
    w.UE(0);
    w.UE(0);
    w.UE(0);
    FinishSpsTail(w);
    return NalOf(w);
}

std::vector<uint8_t> MakePocType1Sps(uint32_t cycle, bool truncateAfterCycle) {
    BitWriter w;
    w.StartNal(3, 7);
    w.U(8, 66);
    w.U(8, 0);
    w.U(8, 42);
    w.UE(0);
    w.UE(0);
    w.UE(1);
    w.U(1, 0);
    w.SE(-1);
    w.SE(1);
    w.UE(cycle);
    if (truncateAfterCycle) {
        w.Trailing();
        return NalOf(w);
    }
    for (uint32_t i = 0; i < cycle; ++i) w.SE(2);
    w.UE(0);
    FinishSpsTail(w);
    return NalOf(w);
}

void CheckRewriteRoundTrips(const std::vector<uint8_t>& nal, const char* what) {
    const std::vector<uint8_t> patched = AnnexBSpsWithZeroReorder(nal);
    Check(!patched.empty(), what);
    if (patched.empty()) return;
    const std::span<const uint8_t> patchedNal =
        std::span<const uint8_t>(patched).subspan(kStartCodeBytes);
    Check(patched[kStartCodeBytes] == kSpsHeaderByte, "the rewrite is still an SPS NAL");
    Check(AnnexBSpsWithZeroReorder(patchedNal).empty(),
        "rewriting it again finds a VUI already there");
}

void TestHighProfileVariantsAreParsed() {
    std::printf("[h264sps] high profiles with chroma and scaling lists are stepped over...\n");
    CheckRewriteRoundTrips(MakeHighProfileSps(44, 3, true),
        "profile 44 with 4:4:4 chroma and scaling lists is rewritten");
    CheckRewriteRoundTrips(MakeHighProfileSps(83, 1, false),
        "profile 83 with 4:2:0 chroma is rewritten");
    CheckRewriteRoundTrips(MakeHighProfileSps(86, 1, true),
        "profile 86 with 8 scaling lists is rewritten");
}

void TestInterlacedAndCroppedSps() {
    std::printf("[h264sps] interlaced and cropped streams keep their flags...\n");
    BitWriter w;
    w.StartNal(3, 7);
    w.U(8, 66);
    w.U(8, 0);
    w.U(8, 42);
    w.UE(0);
    w.UE(0);
    w.UE(0);
    w.UE(0);
    FinishSpsTail(w, false, true);
    CheckRewriteRoundTrips(NalOf(w),
        "an interlaced SPS with frame cropping is rewritten");
}

void TestPocType1IsSteppedOver() {
    std::printf("[h264sps] pic_order_cnt_type 1 offsets are consumed, not misread...\n");
    CheckRewriteRoundTrips(MakePocType1Sps(2, false),
        "an SPS with a 2-entry POC cycle is rewritten");
    Check(AnnexBSpsWithZeroReorder(MakePocType1Sps(50'000, true)).empty(),
        "an absurd POC cycle count is rejected before looping on it");
}

void TestEmulationBytesInTheInputAreUnescaped() {
    std::printf("[h264sps] 00 00 03 in the incoming SPS is stripped before parsing...\n");
    SpsFields f;
    f.profileIdc = 66;
    f.levelIdc = 0;
    const std::vector<uint8_t> clean = MakeSpsNal(f);
    Check(clean[2] == 0 && clean[3] == 0, "the constraint and level bytes form a zero run");

    std::vector<uint8_t> escaped = clean;
    escaped.insert(escaped.begin() + 4, 0x03);
    const std::vector<uint8_t> fromEscaped = AnnexBSpsWithZeroReorder(escaped);
    Check(!fromEscaped.empty(), "the escaped SPS still parses");
    Check(fromEscaped == AnnexBSpsWithZeroReorder(clean),
        "and rewrites to exactly what the unescaped SPS gives");
}

void TestEndlessZeroBitsAreRejected() {
    std::printf("[h264sps] a run of zero bits cannot spin the exp-golomb reader...\n");
    std::vector<uint8_t> nal{0x67, 0x42, 0x00, 0x00};
    nal.insert(nal.end(), 6, 0x00);
    Check(AnnexBSpsWithZeroReorder(nal).empty(),
        "32+ leading zeros in one field give up instead of looping");
}

}

void RunH264SpsTests() {
    TestVuiIsAppended();
    TestPrefixSurvivesUntouched();
    TestExistingVuiIsLeftAlone();
    TestJunkIsRejected();
    TestEmulationPreventionIsApplied();
    TestTrailingBytesAreIgnored();
    TestStreamSpsIsRewrittenInPlace();
    TestStreamWithThreeByteStartCodes();
    TestStreamWithoutSpsIsLeftAlone();
    TestHighProfileVariantsAreParsed();
    TestInterlacedAndCroppedSps();
    TestPocType1IsSteppedOver();
    TestEmulationBytesInTheInputAreUnescaped();
    TestEndlessZeroBitsAreRejected();
}
