#pragma once
#include <span>
#include <string>

namespace deskhub {

struct BindChoice {
    std::string ip{};
    bool fellBack = false;

    bool operator==(const BindChoice&) const = default;
};

BindChoice SelectBindAddress(const std::string& requested,
    std::span<const std::string> available);

}
