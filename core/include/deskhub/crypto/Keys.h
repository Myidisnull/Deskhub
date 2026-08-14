#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

namespace deskhub::crypto {

inline constexpr size_t kKeySize = 32;
inline constexpr size_t kPublicKeySize = 32;
inline constexpr size_t kMacSize = 16;
inline constexpr size_t kNoncePrefixSize = 8;
inline constexpr size_t kAeadOverhead = kNoncePrefixSize + kMacSize;

using RandomFn = std::function<bool(std::span<uint8_t>)>;

struct KeyPair {
    uint8_t sk[kKeySize]{};
    uint8_t pk[kPublicKeySize]{};
};

bool GenerateKeyPair(KeyPair& out, const RandomFn& random);
void PublicFromSecret(uint8_t pk[kPublicKeySize], const uint8_t sk[kKeySize]);

void SecureWipe(std::span<uint8_t> p);

}
