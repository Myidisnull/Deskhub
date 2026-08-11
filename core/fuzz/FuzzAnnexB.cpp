#include "deskhub/media/AnnexB.h"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const std::span<const uint8_t> d(data, size);
    deskhub::media::ParseAnnexB(d);
    deskhub::media::FirstVclOffset(d);
    deskhub::media::ContainsIdr(d);
    if (size > 0) deskhub::media::AvccToAnnexB(d.subspan(1), size_t(d[0] % 10));
    return 0;
}
