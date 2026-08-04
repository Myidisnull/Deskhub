#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace deskhub::media {

struct NalRef {
    size_t offset = 0;
    size_t size = 0;
    size_t startCode = 0;
    uint8_t header = 0;
};

constexpr uint8_t H264NalType(uint8_t header) {
    return header & 0x1F;
}

constexpr bool IsH264Vcl(uint8_t header) {
    return H264NalType(header) >= 1 && H264NalType(header) <= 5;
}

constexpr bool IsH264Idr(uint8_t header) {
    return H264NalType(header) == 5;
}

size_t StartCodeLengthAt(std::span<const uint8_t> data, size_t at);

std::vector<NalRef> ParseAnnexB(std::span<const uint8_t> data);

size_t FirstVclOffset(std::span<const uint8_t> data);

bool ContainsIdr(std::span<const uint8_t> data);

void AppendLengthPrefixed(std::vector<uint8_t>& out, std::span<const uint8_t> nal,
    size_t lengthSize = 4);

std::vector<uint8_t> AvccToAnnexB(std::span<const uint8_t> avcc, size_t lengthSize);

}
