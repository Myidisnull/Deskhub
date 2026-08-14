#pragma once
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

namespace deskhubp {

inline void CopyToBuf(char* dst, size_t cap, std::string_view s) {
    if (!dst || !cap) return;
    const size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
    if (n) std::memcpy(dst, s.data(), n);
    dst[n] = '\0';
}

}
