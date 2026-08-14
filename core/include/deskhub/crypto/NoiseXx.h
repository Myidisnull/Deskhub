#pragma once

#include "deskhub/crypto/Aead.h"
#include "deskhub/crypto/Keys.h"
#include "deskhub/protocol/Wire.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace deskhub::crypto {

inline constexpr size_t kNoiseEphSize = kPublicKeySize;

enum class NoiseRole : uint8_t { Initiator,
    Responder };

class NoiseXx {
public:
    void Reset(NoiseRole role);
    void SetRemoteStatic(const uint8_t pk[kPublicKeySize]);
    void SetLocalStatic(const KeyPair& kp);
    bool SetLocalEphemeral(const RandomFn& random);

    bool BuildMsg1(std::span<uint8_t> out, size_t& n);
    bool AcceptMsg1(std::span<const uint8_t> msg);

    bool BuildMsg2(std::span<uint8_t> out, size_t& n, std::span<const uint8_t> payload);
    bool AcceptMsg2(std::span<const uint8_t> msg, std::span<uint8_t> payloadOut, size_t& payloadN);

    bool BuildMsg3(std::span<uint8_t> out, size_t& n, std::span<const uint8_t> payload);
    bool AcceptMsg3(std::span<const uint8_t> msg, std::span<uint8_t> payloadOut, size_t& payloadN);

    bool Split(uint8_t sendKey[kKeySize], uint8_t recvKey[kKeySize]);

    bool complete() const {
        return complete_;
    }

private:
    void MixHash(std::span<const uint8_t> data);
    void MixKey(std::span<const uint8_t> ikm);
    bool Dh(uint8_t out[kKeySize], const uint8_t sk[kKeySize], const uint8_t pk[kPublicKeySize]);

    NoiseRole role_ = NoiseRole::Initiator;
    KeyPair localStatic_{};
    KeyPair localEph_{};
    uint8_t remoteStatic_[kPublicKeySize]{};
    uint8_t remoteEph_[kPublicKeySize]{};
    bool haveRemoteStatic_ = false;
    bool haveLocalStatic_ = false;
    bool haveLocalEph_ = false;
    bool haveRemoteEph_ = false;
    uint8_t ck_[kKeySize]{};
    uint8_t h_[kKeySize]{};
    bool complete_ = false;
    uint64_t sealCounter_ = 0;
};

inline constexpr uint8_t kHdrFlagEncrypted = 1u << 7;

size_t WrapEncrypted(std::span<uint8_t> out, const CommonHeader& clearHdr, const uint8_t key[kKeySize],
    uint64_t& counter, std::span<const uint8_t> plainPayload);

std::optional<std::vector<uint8_t>> UnwrapEncrypted(std::span<const uint8_t> pkt,
    const uint8_t key[kKeySize], uint64_t& expectedCounter);

}
