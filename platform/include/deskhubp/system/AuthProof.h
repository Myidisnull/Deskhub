#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace deskhubp {

inline constexpr size_t kAuthSaltBytes = 16;
using AuthSalt = std::array<uint8_t, kAuthSaltBytes>;

AuthSalt NewAuthSalt();

}
