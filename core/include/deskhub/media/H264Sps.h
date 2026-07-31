#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace deskhub::media {

std::vector<uint8_t> AnnexBSpsWithZeroReorder(std::span<const uint8_t> spsNal);

std::vector<uint8_t> AnnexBStreamWithZeroReorder(std::span<const uint8_t> annexb);

}
