#include "deskhub/media/AnnexB.h"

namespace deskhub::media {

size_t StartCodeLengthAt(std::span<const uint8_t> data, size_t at) {
    if (at + 3 < data.size() && !data[at] && !data[at + 1] && !data[at + 2] && data[at + 3] == 1)
        return 4;
    if (at + 2 < data.size() && !data[at] && !data[at + 1] && data[at + 2] == 1) return 3;
    return 0;
}

std::vector<NalRef> ParseAnnexB(std::span<const uint8_t> data) {
    std::vector<NalRef> out;
    size_t at = 0;
    while (at < data.size()) {
        const size_t sc = StartCodeLengthAt(data, at);
        if (!sc) {
            ++at;
            continue;
        }
        const size_t start = at + sc;
        size_t end = start;
        while (end < data.size() && !StartCodeLengthAt(data, end)) ++end;
        if (start < end) out.push_back(NalRef{start, end - start, sc, data[start]});
        at = end;
    }
    return out;
}

size_t FirstVclOffset(std::span<const uint8_t> data) {
    for (const NalRef& nal : ParseAnnexB(data))
        if (IsH264Vcl(nal.header)) return nal.offset - nal.startCode;
    return 0;
}

bool ContainsIdr(std::span<const uint8_t> data) {
    size_t at = 0;
    while (at < data.size()) {
        const size_t sc = StartCodeLengthAt(data, at);
        if (!sc) {
            ++at;
            continue;
        }
        const size_t start = at + sc;
        if (start >= data.size()) return false;
        const uint8_t header = data[start];
        if (IsH264Idr(header)) return true;
        at = start;
    }
    return false;
}

void AppendLengthPrefixed(std::vector<uint8_t>& out, std::span<const uint8_t> nal,
    size_t lengthSize) {
    for (size_t i = lengthSize; i > 0; --i)
        out.push_back(uint8_t(nal.size() >> ((i - 1) * 8)));
    out.insert(out.end(), nal.begin(), nal.end());
}

std::vector<uint8_t> AvccToAnnexB(std::span<const uint8_t> avcc, size_t lengthSize) {
    std::vector<uint8_t> out;
    if (!lengthSize || lengthSize > 8) return out;
    out.reserve(avcc.size() + 16);
    size_t at = 0;
    while (at + lengthSize <= avcc.size()) {
        size_t len = 0;
        for (size_t i = 0; i < lengthSize; ++i) len = (len << 8) | avcc[at + i];
        at += lengthSize;
        if (!len || at + len > avcc.size()) break;
        out.push_back(0);
        out.push_back(0);
        out.push_back(0);
        out.push_back(1);
        out.insert(out.end(), avcc.begin() + at, avcc.begin() + at + len);
        at += len;
    }
    return out;
}

}
