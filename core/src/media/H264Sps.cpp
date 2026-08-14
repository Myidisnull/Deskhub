#include "deskhub/media/H264Sps.h"

#include <cstring>

#include "deskhub/media/AnnexB.h"
#include "deskhub/media/BitWriter.h"

namespace deskhub::media {
namespace {

constexpr uint8_t kNalTypeSps = 7;
constexpr uint32_t kEmulationPreventionByte = 0x03;
constexpr uint32_t kMaxMvLengthLog2 = 15;

bool IsHighProfile(uint32_t profileIdc) {
    switch (profileIdc) {
        case 44:
        case 83:
        case 86:
        case 100:
        case 110:
        case 118:
        case 122:
        case 128:
        case 134:
        case 135:
        case 138:
        case 139:
        case 244:
            return true;
        default:
            return false;
    }
}

std::vector<uint8_t> ToRbsp(std::span<const uint8_t> nalPayload) {
    std::vector<uint8_t> rbsp;
    rbsp.reserve(nalPayload.size());
    uint32_t zeros = 0;
    for (const uint8_t b : nalPayload) {
        if (zeros >= 2 && b == kEmulationPreventionByte) {
            zeros = 0;
            continue;
        }
        rbsp.push_back(b);
        zeros = b == 0 ? zeros + 1 : 0;
    }
    return rbsp;
}

class BitReader {
public:
    explicit BitReader(std::span<const uint8_t> bytes)
        : bytes_(bytes) {}

    uint32_t U(uint32_t bits) {
        uint32_t v = 0;
        for (uint32_t i = 0; i < bits; ++i) v = (v << 1) | Bit();
        return v;
    }

    uint32_t UE() {
        uint32_t leading = 0;
        while (!overrun_ && Bit() == 0) {
            if (++leading > 31) {
                overrun_ = true;
                return 0;
            }
        }
        if (!leading) return 0;
        return ((1u << leading) | U(leading)) - 1;
    }

    int32_t SE() {
        const uint32_t k = UE();
        const int32_t half = int32_t((k + 1) / 2);
        return (k & 1) ? half : -half;
    }

    uint32_t position() const {
        return pos_;
    }
    bool overrun() const {
        return overrun_;
    }

private:
    uint32_t Bit() {
        if (pos_ >= bytes_.size() * 8) {
            overrun_ = true;
            return 0;
        }
        const uint32_t b = (bytes_[pos_ >> 3] >> (7 - (pos_ & 7))) & 1;
        ++pos_;
        return b;
    }

    std::span<const uint8_t> bytes_;
    uint32_t pos_ = 0;
    bool overrun_ = false;
};

void SkipScalingList(BitReader& r, uint32_t entries) {
    int32_t last = 8, next = 8;
    for (uint32_t i = 0; i < entries; ++i) {
        if (next) {
            const int64_t delta = r.SE();
            next = int32_t(((int64_t(last) + delta) % 256 + 256) % 256);
        }
        last = next ? next : last;
    }
}

struct SpsPrefix {
    bool valid = false;
    bool hasVui = false;
    uint32_t vuiFlagBit = 0;
    uint32_t maxNumRefFrames = 0;
};

SpsPrefix ParseUpToVuiFlag(std::span<const uint8_t> rbsp) {
    BitReader r(rbsp);
    SpsPrefix sps;

    const uint32_t profileIdc = r.U(8);
    r.U(8);
    r.U(8);
    r.UE();

    if (IsHighProfile(profileIdc)) {
        const uint32_t chromaFormatIdc = r.UE();
        if (chromaFormatIdc == 3) r.U(1);
        r.UE();
        r.UE();
        r.U(1);
        if (r.U(1)) {
            const uint32_t lists = chromaFormatIdc == 3 ? 12 : 8;
            for (uint32_t i = 0; i < lists; ++i)
                if (r.U(1)) SkipScalingList(r, i < 6 ? 16 : 64);
        }
    }

    r.UE();
    const uint32_t pocType = r.UE();
    if (pocType == 0) {
        r.UE();
    } else if (pocType == 1) {
        r.U(1);
        r.SE();
        r.SE();
        const uint32_t cycle = r.UE();
        if (cycle > rbsp.size() * 8) return sps;
        for (uint32_t i = 0; i < cycle; ++i) r.SE();
    }

    sps.maxNumRefFrames = r.UE();
    r.U(1);
    r.UE();
    r.UE();
    if (!r.U(1)) r.U(1);
    r.U(1);
    if (r.U(1)) {
        r.UE();
        r.UE();
        r.UE();
        r.UE();
    }

    sps.vuiFlagBit = r.position();
    sps.hasVui = r.U(1) != 0;
    sps.valid = !r.overrun();
    return sps;
}

void WriteZeroReorderVui(BitWriter& w, uint32_t maxDecFrameBuffering) {
    w.U(1, 0);
    w.U(1, 0);
    w.U(1, 0);
    w.U(1, 0);
    w.U(1, 0);
    w.U(1, 0);
    w.U(1, 0);
    w.U(1, 0);

    w.U(1, 1);
    w.U(1, 1);
    w.UE(0);
    w.UE(0);
    w.UE(kMaxMvLengthLog2);
    w.UE(kMaxMvLengthLog2);
    w.UE(0);
    w.UE(maxDecFrameBuffering);
}

void AppendBytes(std::vector<uint8_t>& out, std::span<const uint8_t> bytes) {
    if (bytes.empty()) return;
    const size_t at = out.size();
    out.resize(at + bytes.size());
    std::memcpy(out.data() + at, bytes.data(), bytes.size());
}

}

std::vector<uint8_t> AnnexBSpsWithZeroReorder(std::span<const uint8_t> spsNal) {
    if (spsNal.size() < 2 || (spsNal[0] & 0x1F) != kNalTypeSps) return {};

    const std::vector<uint8_t> rbsp = ToRbsp(spsNal.subspan(1));
    const SpsPrefix sps = ParseUpToVuiFlag(rbsp);
    if (!sps.valid || sps.hasVui) return {};

    BitWriter w;
    w.StartNal(uint8_t((spsNal[0] >> 5) & 3), kNalTypeSps);
    for (uint32_t bit = 0; bit < sps.vuiFlagBit; ++bit)
        w.U(1, (rbsp[bit >> 3] >> (7 - (bit & 7))) & 1);
    w.U(1, 1);
    WriteZeroReorderVui(w, sps.maxNumRefFrames);
    w.Trailing();
    return w.bytes();
}

std::vector<uint8_t> AnnexBStreamWithZeroReorder(std::span<const uint8_t> annexb) {
    std::vector<uint8_t> out;
    size_t copiedTo = 0;
    size_t at = 0;

    while (at < annexb.size()) {
        const size_t startCode = StartCodeLengthAt(annexb, at);
        if (!startCode) {
            ++at;
            continue;
        }

        const size_t nalStart = at + startCode;
        size_t nalEnd = nalStart;
        while (nalEnd < annexb.size() && !StartCodeLengthAt(annexb, nalEnd)) ++nalEnd;

        if (nalStart < nalEnd && (annexb[nalStart] & 0x1F) == kNalTypeSps) {
            const std::vector<uint8_t> sps =
                AnnexBSpsWithZeroReorder(annexb.subspan(nalStart, nalEnd - nalStart));
            if (!sps.empty()) {
                AppendBytes(out, annexb.subspan(copiedTo, at - copiedTo));
                AppendBytes(out, sps);
                copiedTo = nalEnd;
            }
        }
        at = nalEnd;
    }

    if (out.empty()) return {};
    AppendBytes(out, annexb.subspan(copiedTo));
    return out;
}

}
