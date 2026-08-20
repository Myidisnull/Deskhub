#pragma once
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <string>

namespace deskhubp {

inline void CopyToBuf(char* dst, size_t cap, const std::string& s) {
    if (!dst || !cap) return;
    const size_t n = s.size() < cap - 1 ? s.size() : cap - 1;
    std::memcpy(dst, s.data(), n);
    dst[n] = '\0';
}

inline std::filesystem::path FfiPath(const char* utf8) {
    if (!utf8) return {};
    const std::string text(utf8);
    const std::u8string wide(text.begin(), text.end());
    return std::filesystem::path(wide);
}

}
