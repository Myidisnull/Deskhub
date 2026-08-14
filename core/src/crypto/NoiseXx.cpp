#include "deskhub/crypto/NoiseXx.h"

#include "monocypher.h"

#include <cstring>

namespace deskhub::crypto {
namespace {

constexpr char kPrologue[] = "DeskhubNoiseXX_v1";

void PutU64Be(uint8_t* p, uint64_t v) {
    for (int i = 7; i >= 0; --i) p[i] = uint8_t(v & 0xFF), v >>= 8;
}

uint64_t GetU64Be(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
    return v;
}

}

void NoiseXx::Reset(NoiseRole role) {
    role_ = role;
    haveRemoteStatic_ = false;
    haveLocalStatic_ = false;
    haveLocalEph_ = false;
    haveRemoteEph_ = false;
    complete_ = false;
    sealCounter_ = 0;
    std::memset(ck_, 0, sizeof(ck_));
    std::memset(h_, 0, sizeof(h_));
    crypto_blake2b(h_, kKeySize, reinterpret_cast<const uint8_t*>(kPrologue), sizeof(kPrologue) - 1);
    std::memcpy(ck_, h_, kKeySize);
}

void NoiseXx::SetRemoteStatic(const uint8_t pk[kPublicKeySize]) {
    std::memcpy(remoteStatic_, pk, kPublicKeySize);
    haveRemoteStatic_ = true;
}

void NoiseXx::SetLocalStatic(const KeyPair& kp) {
    localStatic_ = kp;
    haveLocalStatic_ = true;
}

bool NoiseXx::SetLocalEphemeral(const RandomFn& random) {
    if (!GenerateKeyPair(localEph_, random)) return false;
    haveLocalEph_ = true;
    return true;
}

void NoiseXx::MixHash(std::span<const uint8_t> data) {
    crypto_blake2b_ctx ctx;
    crypto_blake2b_init(&ctx, kKeySize);
    crypto_blake2b_update(&ctx, h_, kKeySize);
    crypto_blake2b_update(&ctx, data.data(), data.size());
    crypto_blake2b_final(&ctx, h_);
}

void NoiseXx::MixKey(std::span<const uint8_t> ikm) {
    uint8_t tmp[kKeySize];
    HkdfBlake2b(tmp, ikm, std::span<const uint8_t>(ck_, kKeySize));
    std::memcpy(ck_, tmp, kKeySize);
    SecureWipe(std::span<uint8_t>(tmp, sizeof(tmp)));
}

bool NoiseXx::Dh(uint8_t out[kKeySize], const uint8_t sk[kKeySize], const uint8_t pk[kPublicKeySize]) {
    crypto_x25519(out, sk, pk);
    static const uint8_t zeros[kKeySize] = {};
    if (std::memcmp(out, zeros, kKeySize) == 0) return false;
    return true;
}

bool NoiseXx::BuildMsg1(std::span<uint8_t> out, size_t& n) {
    if (role_ != NoiseRole::Initiator || !haveLocalEph_) return false;
    if (out.size() < kNoiseEphSize) return false;
    std::memcpy(out.data(), localEph_.pk, kNoiseEphSize);
    MixHash(std::span<const uint8_t>(localEph_.pk, kNoiseEphSize));
    n = kNoiseEphSize;
    return true;
}

bool NoiseXx::AcceptMsg1(std::span<const uint8_t> msg) {
    if (role_ != NoiseRole::Responder || msg.size() < kNoiseEphSize) return false;
    std::memcpy(remoteEph_, msg.data(), kNoiseEphSize);
    haveRemoteEph_ = true;
    MixHash(std::span<const uint8_t>(remoteEph_, kNoiseEphSize));
    return true;
}

bool NoiseXx::BuildMsg2(std::span<uint8_t> out, size_t& n, std::span<const uint8_t> payload) {
    if (role_ != NoiseRole::Responder || !haveLocalEph_ || !haveLocalStatic_ || !haveRemoteEph_)
        return false;
    uint8_t dh[kKeySize];
    if (!Dh(dh, localEph_.sk, remoteEph_)) return false;
    MixKey(dh);
    MixHash(std::span<const uint8_t>(localEph_.pk, kNoiseEphSize));
    SecureWipe(std::span<uint8_t>(dh, sizeof(dh)));

    if (!Dh(dh, localStatic_.sk, remoteEph_)) return false;
    MixKey(dh);
    SecureWipe(std::span<uint8_t>(dh, sizeof(dh)));

    MixHash(std::span<const uint8_t>(localStatic_.pk, kPublicKeySize));

    const size_t need = kNoiseEphSize + kPublicKeySize + payload.size() + kMacSize;
    if (out.size() < need) return false;
    std::memcpy(out.data(), localEph_.pk, kNoiseEphSize);
    std::memcpy(out.data() + kNoiseEphSize, localStatic_.pk, kPublicKeySize);

    uint8_t k[kKeySize];
    HkdfBlake2b(k, std::span<const uint8_t>(ck_, kKeySize),
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>("msg2"), 4));
    if (!AeadSeal(std::span<uint8_t>(out.data() + kNoiseEphSize + kPublicKeySize, payload.size() + kMacSize),
            k, sealCounter_++, std::span<const uint8_t>(h_, kKeySize), payload)) {
        SecureWipe(std::span<uint8_t>(k, sizeof(k)));
        return false;
    }
    SecureWipe(std::span<uint8_t>(k, sizeof(k)));
    MixHash(std::span<const uint8_t>(out.data() + kNoiseEphSize + kPublicKeySize, payload.size() + kMacSize));
    n = need;
    return true;
}

bool NoiseXx::AcceptMsg2(std::span<const uint8_t> msg, std::span<uint8_t> payloadOut, size_t& payloadN) {
    if (role_ != NoiseRole::Initiator || !haveLocalEph_) return false;
    if (msg.size() < kNoiseEphSize + kPublicKeySize + kMacSize) return false;
    std::memcpy(remoteEph_, msg.data(), kNoiseEphSize);
    haveRemoteEph_ = true;
    std::memcpy(remoteStatic_, msg.data() + kNoiseEphSize, kPublicKeySize);
    haveRemoteStatic_ = true;

    uint8_t dh[kKeySize];
    if (!Dh(dh, localEph_.sk, remoteEph_)) return false;
    MixKey(dh);
    MixHash(std::span<const uint8_t>(remoteEph_, kNoiseEphSize));
    SecureWipe(std::span<uint8_t>(dh, sizeof(dh)));

    if (!Dh(dh, localEph_.sk, remoteStatic_)) return false;
    MixKey(dh);
    SecureWipe(std::span<uint8_t>(dh, sizeof(dh)));

    MixHash(std::span<const uint8_t>(remoteStatic_, kPublicKeySize));

    const size_t ct = msg.size() - kNoiseEphSize - kPublicKeySize;
    if (payloadOut.size() < ct - kMacSize) return false;
    uint8_t k[kKeySize];
    HkdfBlake2b(k, std::span<const uint8_t>(ck_, kKeySize),
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>("msg2"), 4));
    if (!AeadOpen(std::span<uint8_t>(payloadOut.data(), ct - kMacSize), k, 0,
            std::span<const uint8_t>(h_, kKeySize),
            std::span<const uint8_t>(msg.data() + kNoiseEphSize + kPublicKeySize, ct))) {
        SecureWipe(std::span<uint8_t>(k, sizeof(k)));
        return false;
    }
    SecureWipe(std::span<uint8_t>(k, sizeof(k)));
    MixHash(std::span<const uint8_t>(msg.data() + kNoiseEphSize + kPublicKeySize, ct));
    payloadN = ct - kMacSize;
    return true;
}

bool NoiseXx::BuildMsg3(std::span<uint8_t> out, size_t& n, std::span<const uint8_t> payload) {
    if (role_ != NoiseRole::Initiator || !haveLocalEph_ || !haveLocalStatic_ || !haveRemoteEph_ ||
        !haveRemoteStatic_)
        return false;

    uint8_t dh[kKeySize];
    if (!Dh(dh, localStatic_.sk, remoteEph_)) return false;
    MixKey(dh);
    SecureWipe(std::span<uint8_t>(dh, sizeof(dh)));

    MixHash(std::span<const uint8_t>(localStatic_.pk, kPublicKeySize));

    const size_t need = kPublicKeySize + payload.size() + kMacSize;
    if (out.size() < need) return false;
    std::memcpy(out.data(), localStatic_.pk, kPublicKeySize);

    uint8_t k[kKeySize];
    HkdfBlake2b(k, std::span<const uint8_t>(ck_, kKeySize),
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>("msg3"), 4));
    if (!AeadSeal(std::span<uint8_t>(out.data() + kPublicKeySize, payload.size() + kMacSize), k,
            sealCounter_++, std::span<const uint8_t>(h_, kKeySize), payload)) {
        SecureWipe(std::span<uint8_t>(k, sizeof(k)));
        return false;
    }
    SecureWipe(std::span<uint8_t>(k, sizeof(k)));
    MixHash(std::span<const uint8_t>(out.data() + kPublicKeySize, payload.size() + kMacSize));
    n = need;
    complete_ = true;
    return true;
}

bool NoiseXx::AcceptMsg3(std::span<const uint8_t> msg, std::span<uint8_t> payloadOut, size_t& payloadN) {
    if (role_ != NoiseRole::Responder || !haveLocalEph_ || !haveRemoteEph_) return false;
    if (msg.size() < kPublicKeySize + kMacSize) return false;
    std::memcpy(remoteStatic_, msg.data(), kPublicKeySize);
    haveRemoteStatic_ = true;

    uint8_t dh[kKeySize];
    if (!Dh(dh, localEph_.sk, remoteStatic_)) return false;
    MixKey(dh);
    SecureWipe(std::span<uint8_t>(dh, sizeof(dh)));

    MixHash(std::span<const uint8_t>(remoteStatic_, kPublicKeySize));

    const size_t ct = msg.size() - kPublicKeySize;
    if (payloadOut.size() < ct - kMacSize) return false;
    uint8_t k[kKeySize];
    HkdfBlake2b(k, std::span<const uint8_t>(ck_, kKeySize),
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>("msg3"), 4));
    if (!AeadOpen(std::span<uint8_t>(payloadOut.data(), ct - kMacSize), k, 0,
            std::span<const uint8_t>(h_, kKeySize),
            std::span<const uint8_t>(msg.data() + kPublicKeySize, ct))) {
        SecureWipe(std::span<uint8_t>(k, sizeof(k)));
        return false;
    }
    SecureWipe(std::span<uint8_t>(k, sizeof(k)));
    MixHash(std::span<const uint8_t>(msg.data() + kPublicKeySize, ct));
    payloadN = ct - kMacSize;
    complete_ = true;
    return true;
}

bool NoiseXx::Split(uint8_t sendKey[kKeySize], uint8_t recvKey[kKeySize]) {
    if (!complete_) return false;
    HkdfBlake2b(sendKey, std::span<const uint8_t>(ck_, kKeySize),
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>("send"), 4));
    HkdfBlake2b(recvKey, std::span<const uint8_t>(ck_, kKeySize),
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>("recv"), 4));
    if (role_ == NoiseRole::Responder) {
        uint8_t tmp[kKeySize];
        std::memcpy(tmp, sendKey, kKeySize);
        std::memcpy(sendKey, recvKey, kKeySize);
        std::memcpy(recvKey, tmp, kKeySize);
        SecureWipe(std::span<uint8_t>(tmp, sizeof(tmp)));
    }
    return true;
}

size_t WrapEncrypted(std::span<uint8_t> out, const CommonHeader& clearHdr, const uint8_t key[kKeySize],
    uint64_t& counter, std::span<const uint8_t> plainPayload) {
    if (out.size() < kCommonHeaderSize + kAeadOverhead + plainPayload.size()) return 0;
    CommonHeader hdr = clearHdr;
    hdr.flags = uint8_t(hdr.flags | kHdrFlagEncrypted);
    WriteCommonHeader(out, hdr);
    uint8_t* p = out.data() + kCommonHeaderSize;
    PutU64Be(p, counter);
    uint8_t ad[kCommonHeaderSize];
    WriteCommonHeader(ad, hdr);
    if (!AeadSeal(std::span<uint8_t>(p + kNoncePrefixSize, plainPayload.size() + kMacSize), key, counter,
            std::span<const uint8_t>(ad, kCommonHeaderSize), plainPayload))
        return 0;
    ++counter;
    return kCommonHeaderSize + kAeadOverhead + plainPayload.size();
}

std::optional<std::vector<uint8_t>> UnwrapEncrypted(std::span<const uint8_t> pkt,
    const uint8_t key[kKeySize], uint64_t& expectedCounter) {
    const auto h = ParseCommonHeader(pkt);
    if (!h || !(h->flags & kHdrFlagEncrypted)) return std::nullopt;
    if (pkt.size() < kCommonHeaderSize + kAeadOverhead) return std::nullopt;
    const uint64_t counter = GetU64Be(pkt.data() + kCommonHeaderSize);
    if (counter < expectedCounter) return std::nullopt;
    const size_t body = pkt.size() - kCommonHeaderSize - kNoncePrefixSize;
    if (body < kMacSize) return std::nullopt;
    const size_t plainLen = body - kMacSize;
    std::vector<uint8_t> plain(plainLen);
    uint8_t ad[kCommonHeaderSize];
    WriteCommonHeader(std::span<uint8_t>(ad, sizeof(ad)), *h);
    if (!AeadOpen(std::span<uint8_t>(plain.data(), plain.size()), key, counter,
            std::span<const uint8_t>(ad, kCommonHeaderSize),
            std::span<const uint8_t>(pkt.data() + kCommonHeaderSize + kNoncePrefixSize, body)))
        return std::nullopt;
    expectedCounter = counter + 1;
    return plain;
}

}
