#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

namespace deskhub {

inline constexpr size_t kMaxSafeNameBytes = 255;
inline constexpr size_t kMaxSafeNameAttempts = 1000;

std::string SafeFileName(std::string_view name);

std::string UniqueFileName(std::string_view safeName,
    const std::function<bool(const std::string&)>& taken);

}
