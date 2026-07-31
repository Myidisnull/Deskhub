#include "Tests.h"
#include "support/TestSupport.h"

#include "deskhub/media/BitWriter.h"

#include <cstdio>
#include <utility>
#include <vector>

using deskhub::media::BitWriter;

namespace {

constexpr size_t kStartCodeAndHeaderBytes = 5;

std::vector<uint8_t> Unescape(const std::vector<uint8_t>& in, size_t from) {
    std::vector<uint8_t> out;
    uint32_t zeros = 0;
    for (size_t i = from; i < in.size(); ++i) {
        const uint8_t b = in[i];
        if (zeros >= 2 && b == 0x03) {
            zeros = 0;
            continue;
        }
        out.push_back(b);
        zeros = b == 0 ? zeros + 1 : 0;
    }
    return out;
}

class BitReader {
public:
    explicit BitReader(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}

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

    int32_t SE() {
        const uint32_t k = UE();
        const int32_t half = int32_t((k + 1) / 2);
        return (k & 1) ? half : -half;
    }

private:
    bool Exhausted() const {
        return pos_ >= bytes_.size() * 8;
    }

    uint32_t Bit() {
        if (Exhausted()) return 0;
        const uint32_t b = (bytes_[pos_ / 8] >> (7 - pos_ % 8)) & 1u;
        ++pos_;
        return b;
    }

    std::vector<uint8_t> bytes_;
    size_t pos_ = 0;
};

BitReader PayloadReader(const BitWriter& w) {
    return BitReader(Unescape(w.bytes(), kStartCodeAndHeaderBytes));
}

void TestFixedWidthBits() {
    std::printf("[bitwriter] U() packs MSB-first and spills into the next byte...\n");
    BitWriter w;
    w.U(3, 5);
    w.U(5, 17);
    Check(w.bytes() == std::vector<uint8_t>{0xB1}, "101 + 10001 -> one byte 0xB1");

    BitWriter x;
    x.U(4, 0xF);
    Check(x.bytes().empty(), "a half-written byte is not emitted yet");
    x.U(4, 0x0);
    Check(x.bytes() == std::vector<uint8_t>{0xF0}, "the byte appears once 8 bits are in");

    BitWriter y;
    y.U(32, 0xDEADBEEF);
    Check(y.bytes() == std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}, "a full 32-bit value");
}

void TestStartCode() {
    std::printf("[bitwriter] StartNal() emits the 4-byte start code plus the NAL header...\n");
    BitWriter w;
    w.StartNal(3, 7);
    Check(w.bytes() == std::vector<uint8_t>{0, 0, 0, 1, 0x67}, "ref_idc 3 + type 7 -> 0x67");

    BitWriter x;
    x.StartNal(3, 8);
    Check(x.bytes() == std::vector<uint8_t>{0, 0, 0, 1, 0x68}, "ref_idc 3 + type 8 -> 0x68");

    BitWriter y;
    y.U(4, 0xA);
    y.StartNal(0, 1);
    Check(y.bytes() == std::vector<uint8_t>{0xA0, 0, 0, 0, 1, 0x01},
        "a pending partial byte is flushed with zeros before the start code");
}

void TestExpGolombRoundTrip() {
    std::printf("[bitwriter] UE()/SE() round-trip through an exp-golomb reader...\n");
    const uint32_t unsignedValues[] = {0, 1, 2, 3, 4, 7, 8, 15, 16, 255, 256, 65535, 1'000'000};
    BitWriter w;
    w.StartNal(3, 7);
    for (uint32_t v : unsignedValues) w.UE(v);
    w.Trailing();

    BitReader r = PayloadReader(w);
    bool ok = true;
    for (uint32_t v : unsignedValues) ok = ok && r.UE() == v;
    Check(ok, "every unsigned value decodes back to itself");

    const int32_t signedValues[] = {0, 1, -1, 2, -2, 3, -3, 127, -128, 4096, -4096};
    BitWriter s;
    s.StartNal(3, 7);
    for (int32_t v : signedValues) s.SE(v);
    s.Trailing();

    BitReader sr = PayloadReader(s);
    bool signedOk = true;
    for (int32_t v : signedValues) signedOk = signedOk && sr.SE() == v;
    Check(signedOk, "every signed value decodes back to itself");
}

void TestExpGolombEncoding() {
    std::printf("[bitwriter] UE() matches the published code words...\n");
    BitWriter w;
    w.UE(0);
    w.UE(1);
    w.UE(2);
    w.U(3, 0);
    Check(w.bytes() == std::vector<uint8_t>{0b10100110},
        "UE(0)=1, UE(1)=010, UE(2)=011 packed into one byte");
}

void TestEmulationPrevention() {
    std::printf("[bitwriter] three zero bytes in a row get an escape byte...\n");
    BitWriter w;
    w.StartNal(3, 7);
    w.U(8, 0);
    w.U(8, 0);
    w.U(8, 0);
    Check(w.bytes() == std::vector<uint8_t>{0, 0, 0, 1, 0x67, 0x00, 0x00, 0x03, 0x00},
        "0x00 0x00 0x00 is written as 0x00 0x00 0x03 0x00");
    Check(Unescape(w.bytes(), kStartCodeAndHeaderBytes) == std::vector<uint8_t>{0, 0, 0},
        "a decoder strips the escape and recovers the three zeros");

    BitWriter x;
    x.StartNal(3, 7);
    x.U(8, 0);
    x.U(8, 0);
    x.U(8, 4);
    Check(x.bytes().size() == kStartCodeAndHeaderBytes + 3,
        "a byte above 0x03 after two zeros needs no escape");

    BitWriter y;
    y.StartNal(3, 7);
    y.U(8, 0);
    y.U(8, 0);
    y.U(8, 1);
    y.U(8, 0);
    y.U(8, 0);
    y.U(8, 2);
    Check(y.bytes() == std::vector<uint8_t>{0, 0, 0, 1, 0x67, 0, 0, 3, 1, 0, 0, 3, 2},
        "the zero run restarts after each escape");
}

void TestStartNalResetsZeroRun() {
    std::printf("[bitwriter] a fresh NAL does not inherit the previous zero run...\n");
    BitWriter w;
    w.StartNal(3, 7);
    w.U(8, 0);
    w.U(8, 0);
    w.StartNal(3, 8);
    w.U(8, 0);
    Check(w.bytes() == std::vector<uint8_t>{0, 0, 0, 1, 0x67, 0, 0, 0, 0, 0, 1, 0x68, 0},
        "the start code is literal and clears the run, so no escape follows it");
}

void TestTrailingBits() {
    std::printf("[bitwriter] Trailing() writes the stop bit then pads with zeros...\n");
    BitWriter w;
    w.StartNal(3, 8);
    w.UE(0);
    w.Trailing();
    Check(w.bytes() == std::vector<uint8_t>{0, 0, 0, 1, 0x68, 0b11000000},
        "one payload bit + stop bit + six zero bits");

    BitWriter x;
    x.U(8, 0xFF);
    x.Trailing();
    Check(x.bytes() == std::vector<uint8_t>{0xFF, 0b10000000},
        "an already-aligned payload still gets a whole stop byte");
}

void TestBitLengthAndClear() {
    std::printf("[bitwriter] bitLength() counts emitted bytes; Clear() starts over...\n");
    BitWriter w;
    w.StartNal(3, 7);
    Check(w.bitLength() == 40, "start code plus NAL header is 5 bytes");
    w.U(8, 0xAB);
    Check(w.bitLength() == 48, "one payload byte adds 8 bits");
    w.U(4, 0);
    Check(w.bitLength() == 48, "a partial byte is not counted until it is emitted");

    w.Clear();
    Check(w.bytes().empty() && w.bitLength() == 0, "Clear() empties the buffer");
    w.U(8, 0);
    w.U(8, 0);
    w.U(8, 0);
    Check(w.bytes() == std::vector<uint8_t>{0, 0, 3, 0},
        "Clear() also drops the pending partial byte and resets the zero run");
}

void TestSpsPrologue() {
    std::printf("[bitwriter] the SPS prologue the VA-API encoder writes reads back...\n");
    constexpr uint32_t kMbW = 1920 / 16, kMbH = 1088 / 16;
    BitWriter w;
    w.StartNal(3, 7);
    w.U(8, 77);
    w.U(8, 0);
    w.U(8, 40);
    w.UE(0);
    w.UE(12);
    w.UE(0);
    w.UE(12);
    w.UE(1);
    w.U(1, 0);
    w.UE(kMbW - 1);
    w.UE(kMbH - 1);
    w.Trailing();

    BitReader r = PayloadReader(w);
    Check(r.U(8) == 77, "profile_idc");
    Check(r.U(8) == 0, "constraint flags");
    Check(r.U(8) == 40, "level_idc");
    Check(r.UE() == 0, "seq_parameter_set_id");
    Check(r.UE() == 12, "log2_max_frame_num_minus4");
    Check(r.UE() == 0, "pic_order_cnt_type");
    Check(r.UE() == 12, "log2_max_pic_order_cnt_lsb_minus4");
    Check(r.UE() == 1, "max_num_ref_frames");
    Check(r.U(1) == 0, "gaps_in_frame_num_value_allowed_flag");
    Check(r.UE() == kMbW - 1, "pic_width_in_mbs_minus1");
    Check(r.UE() == kMbH - 1, "pic_height_in_map_units_minus1");
}

}

void RunBitWriterTests() {
    TestFixedWidthBits();
    TestStartCode();
    TestExpGolombRoundTrip();
    TestExpGolombEncoding();
    TestEmulationPrevention();
    TestStartNalResetsZeroRun();
    TestTrailingBits();
    TestBitLengthAndClear();
    TestSpsPrologue();
}
