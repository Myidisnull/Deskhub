#pragma once

#include "deskhub/crypto/Keys.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace deskhub::crypto {

bool AeadSeal(std::span<uint8_t> outCipherAndMac, const uint8_t key[kKeySize], uint64_t counter,
    std::span<const uint8_t> ad, std::span<const uint8_t> plain);

bool AeadOpen(std::span<uint8_t> outPlain, const uint8_t key[kKeySize], uint64_t counter,
    std::span<const uint8_t> ad, std::span<const uint8_t> cipherAndMac);

void HkdfBlake2b(uint8_t out[kKeySize], std::span<const uint8_t> ikm, std::span<const uint8_t> info);

}
