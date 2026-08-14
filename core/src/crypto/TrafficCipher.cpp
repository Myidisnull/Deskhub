#include "deskhub/crypto/TrafficCipher.h"

#include "deskhub/crypto/NoiseXx.h"

#include <cstring>

namespace deskhub::crypto {

void TrafficCipher::Reset() {
    std::lock_guard<std::mutex> lk(mu_);
    SecureWipe(std::span<uint8_t>(key_, kKeySize));
    hasKey_ = false;
    sendCounter_ = 0;
    recvCounter_ = 0;
}

void TrafficCipher::SetKey(const uint8_t key[kKeySize]) {
    std::lock_guard<std::mutex> lk(mu_);
    std::memcpy(key_, key, kKeySize);
    hasKey_ = true;
    sendCounter_ = 0;
    recvCounter_ = 0;
}

size_t TrafficCipher::SealInto(std::span<uint8_t> out, MsgType type, Chan chan, uint32_t sessionId,
    std::span<const uint8_t> plainPayload) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!hasKey_) return 0;
    CommonHeader hdr{kProtocolVersion, type, 0, chan, sessionId};
    return WrapEncrypted(out, hdr, key_, sendCounter_, plainPayload);
}

size_t TrafficCipher::SealDatagram(std::span<uint8_t> out, std::span<const uint8_t> clearPkt) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!hasKey_) return 0;
    const auto h = ParseCommonHeader(clearPkt);
    if (!h) return 0;
    return WrapEncrypted(out, *h, key_, sendCounter_, PayloadOf(clearPkt));
}

std::optional<std::vector<uint8_t>> TrafficCipher::OpenPayload(std::span<const uint8_t> pkt,
    uint64_t& recvCounter) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!hasKey_) return std::nullopt;
    return UnwrapEncrypted(pkt, key_, recvCounter);
}

std::optional<std::vector<uint8_t>> TrafficCipher::OpenDatagram(std::span<const uint8_t> pkt,
    uint64_t& recvCounter) {
    const auto h = ParseCommonHeader(pkt);
    if (!h) return std::nullopt;
    if (!(h->flags & kHdrFlagEncrypted)) {
        return std::vector<uint8_t>(pkt.begin(), pkt.end());
    }
    std::optional<std::vector<uint8_t>> plain;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!hasKey_) return std::nullopt;
        plain = UnwrapEncrypted(pkt, key_, recvCounter);
    }
    if (!plain) return std::nullopt;
    std::vector<uint8_t> out(kCommonHeaderSize + plain->size());
    CommonHeader clear = *h;
    clear.flags = uint8_t(clear.flags & ~kHdrFlagEncrypted);
    WriteCommonHeader(std::span<uint8_t>(out.data(), out.size()), clear);
    if (!plain->empty())
        std::memcpy(out.data() + kCommonHeaderSize, plain->data(), plain->size());
    return out;
}

std::optional<std::vector<uint8_t>> TrafficCipher::OpenPayload(std::span<const uint8_t> pkt) {
    return OpenPayload(pkt, recvCounter_);
}

std::optional<std::vector<uint8_t>> TrafficCipher::OpenDatagram(std::span<const uint8_t> pkt) {
    return OpenDatagram(pkt, recvCounter_);
}

}
