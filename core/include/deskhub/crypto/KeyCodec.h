#pragma once

#include "deskhub/crypto/Keys.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace deskhub::crypto {

std::string KeyToHex(std::span<const uint8_t> key);
bool KeyFromHex(std::string_view hex, std::span<uint8_t> out);

bool LoadOrCreateHostStaticKey(std::string& hexInOut, KeyPair& out, const RandomFn& random);

bool LoadOrCreateSessionKey(std::string& hexInOut, uint8_t out[kKeySize], const RandomFn& random);
bool RefreshSessionKey(std::string& hexInOut, uint8_t out[kKeySize], const RandomFn& random);

}
