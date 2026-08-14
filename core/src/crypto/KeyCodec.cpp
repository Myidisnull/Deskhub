#include "deskhub/crypto/KeyCodec.h"

#include <cctype>

namespace deskhub::crypto {
namespace {

int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}

std::string KeyToHex(std::span<const uint8_t> key) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.resize(key.size() * 2);
    for (size_t i = 0; i < key.size(); ++i) {
        out[i * 2] = kDigits[key[i] >> 4];
        out[i * 2 + 1] = kDigits[key[i] & 0xF];
    }
    return out;
}

bool KeyFromHex(std::string_view hex, std::span<uint8_t> out) {
    if (hex.size() != out.size() * 2) return false;
    for (size_t i = 0; i < out.size(); ++i) {
        const int hi = HexVal(hex[i * 2]);
        const int lo = HexVal(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = uint8_t((hi << 4) | lo);
    }
    return true;
}

bool LoadOrCreateHostStaticKey(std::string& hexInOut, KeyPair& out, const RandomFn& random) {
    if (KeyFromHex(hexInOut, std::span<uint8_t>(out.sk, kKeySize))) {
        PublicFromSecret(out.pk, out.sk);
        return true;
    }
    if (!GenerateKeyPair(out, random)) return false;
    hexInOut = KeyToHex(std::span<const uint8_t>(out.sk, kKeySize));
    return true;
}

bool LoadOrCreateSessionKey(std::string& hexInOut, uint8_t out[kKeySize], const RandomFn& random) {
    if (KeyFromHex(hexInOut, std::span<uint8_t>(out, kKeySize))) return true;
    return RefreshSessionKey(hexInOut, out, random);
}

bool RefreshSessionKey(std::string& hexInOut, uint8_t out[kKeySize], const RandomFn& random) {
    if (!random || !random(std::span<uint8_t>(out, kKeySize))) return false;
    hexInOut = KeyToHex(std::span<const uint8_t>(out, kKeySize));
    return true;
}

}
