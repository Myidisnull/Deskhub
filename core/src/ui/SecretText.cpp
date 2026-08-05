#include "deskhub/ui/SecretText.h"

#include <cstdint>

namespace deskhub::ui {

namespace {

constexpr uint8_t kKey[] = {0x5A, 0x9C, 0x33, 0xE7, 0x11, 0x8B, 0x46, 0xD2};
constexpr char kHexDigits[] = "0123456789abcdef";

uint8_t KeyAt(size_t index) {
    return uint8_t(kKey[index % sizeof(kKey)] ^ uint8_t(index * 31 + 7));
}

int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}

std::string EncodeSecret(std::string_view plain) {
    if (plain.empty()) return {};

    std::string out(1, kSecretPrefix);
    out.reserve(1 + plain.size() * 2);
    for (size_t i = 0; i < plain.size(); ++i) {
        const uint8_t mixed = uint8_t(uint8_t(plain[i]) ^ KeyAt(i));
        out.push_back(kHexDigits[mixed >> 4]);
        out.push_back(kHexDigits[mixed & 0x0F]);
    }
    return out;
}

std::string DecodeSecret(std::string_view stored) {
    if (stored.empty() || stored.front() != kSecretPrefix) return std::string(stored);

    const std::string_view body = stored.substr(1);
    if (body.size() % 2 != 0) return {};

    std::string out;
    out.reserve(body.size() / 2);
    for (size_t i = 0; i < body.size(); i += 2) {
        const int hi = HexValue(body[i]);
        const int lo = HexValue(body[i + 1]);
        if (hi < 0 || lo < 0) return {};
        const uint8_t mixed = uint8_t((hi << 4) | lo);
        out.push_back(char(mixed ^ KeyAt(i / 2)));
    }
    return out;
}

}
