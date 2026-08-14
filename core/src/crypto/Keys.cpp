#include "deskhub/crypto/Keys.h"

#include "monocypher.h"

namespace deskhub::crypto {

bool GenerateKeyPair(KeyPair& out, const RandomFn& random) {
    if (!random || !random(std::span<uint8_t>(out.sk, kKeySize))) return false;
    crypto_x25519_public_key(out.pk, out.sk);
    return true;
}

void PublicFromSecret(uint8_t pk[kPublicKeySize], const uint8_t sk[kKeySize]) {
    crypto_x25519_public_key(pk, sk);
}

void SecureWipe(std::span<uint8_t> p) {
    if (!p.empty()) crypto_wipe(p.data(), p.size());
}

}
