#include "deskhub/media/H264Sps.h"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const std::span<const uint8_t> d(data, size);
    deskhub::media::AnnexBSpsWithZeroReorder(d);
    const auto out = deskhub::media::AnnexBStreamWithZeroReorder(d);
    if (!out.empty()) deskhub::media::AnnexBStreamWithZeroReorder(out);
    return 0;
}
