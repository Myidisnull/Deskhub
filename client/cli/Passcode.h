#pragma once
#include <string>

#include "deskhub/cli/Command.h"

namespace deskhubcli {

struct Passcode {
    bool ok = false;
    std::string value{};
    std::string error{};
};

Passcode ResolvePasscode(const deskhub::cli::Command& command);

}
