#include "deskhub/crypto/Aead.h"

#include "monocypher.h"

#include <cstring>

namespace deskhub::crypto {
namespace {

void CounterNonce(uint8_t nonce[12], uint64_t counter) {
    std::memset(nonce, 0, 12);
    for (int i = 0; i < 8; ++i) nonce[4 + i] = uint8_t((counter >> (56 - 8 * i)) & 0xFF);
}

}

bool AeadSeal(std::span<uint8_t> outCipherAndMac, const uint8_t key[kKeySize], uint64_t counter,
    std::span<const uint8_t> ad, std::span<const uint8_t> plain) {
    if (outCipherAndMac.size() < plain.size() + kMacSize) return false;
    uint8_t nonce[12];
    CounterNonce(nonce, counter);
    crypto_aead_ctx ctx;
    crypto_aead_init_ietf(&ctx, key, nonce);
    crypto_aead_write(&ctx, outCipherAndMac.data(), outCipherAndMac.data() + plain.size(), ad.data(),
        ad.size(), plain.data(), plain.size());
    crypto_wipe(&ctx, sizeof(ctx));
    return true;
}

bool AeadOpen(std::span<uint8_t> outPlain, const uint8_t key[kKeySize], uint64_t counter,
    std::span<const uint8_t> ad, std::span<const uint8_t> cipherAndMac) {
    if (cipherAndMac.size() < kMacSize) return false;
    const size_t text = cipherAndMac.size() - kMacSize;
    if (outPlain.size() < text) return false;
    uint8_t nonce[12];
    CounterNonce(nonce, counter);
    crypto_aead_ctx ctx;
    crypto_aead_init_ietf(&ctx, key, nonce);
    const int ok = crypto_aead_read(&ctx, outPlain.data(), cipherAndMac.data() + text, ad.data(),
        ad.size(), cipherAndMac.data(), text);
    crypto_wipe(&ctx, sizeof(ctx));
    return ok == 0;
}

void HkdfBlake2b(uint8_t out[kKeySize], std::span<const uint8_t> ikm, std::span<const uint8_t> info) {
    uint8_t prk[kKeySize];
    crypto_blake2b_keyed(prk, kKeySize, nullptr, 0, ikm.data(), ikm.size());
    crypto_blake2b_ctx ctx;
    crypto_blake2b_keyed_init(&ctx, kKeySize, prk, kKeySize);
    crypto_blake2b_update(&ctx, info.data(), info.size());
    const uint8_t one = 1;
    crypto_blake2b_update(&ctx, &one, 1);
    crypto_blake2b_final(&ctx, out);
    crypto_wipe(prk, sizeof(prk));
}

}
