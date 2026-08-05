#pragma once
#include <string>
#include <string_view>

namespace deskhub::ui {

inline constexpr char kSecretPrefix = '@';

std::string EncodeSecret(std::string_view plain);
std::string DecodeSecret(std::string_view stored);

}
