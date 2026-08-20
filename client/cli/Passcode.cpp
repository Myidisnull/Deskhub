#include "Passcode.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "deskhub/protocol/Wire.h"
#include "deskhub/ui/Strings.h"
#include "deskhubp/system/Console.h"

namespace deskhubcli {

namespace {

constexpr const char* kPasscodeEnvVar = "DESKHUB_PASSCODE";

std::string FirstLine(std::string_view text) {
    const size_t end = text.find_first_of("\r\n");
    return std::string(end == std::string_view::npos ? text : text.substr(0, end));
}

Passcode Accept(std::string value) {
    Passcode result;
    if (!deskhub::IsValidPasscode(value)) {
        result.error = deskhub::ui::kPasscodeInvalid;
        return result;
    }
    result.ok = true;
    result.value = std::move(value);
    return result;
}

Passcode Fail(std::string error) {
    Passcode result;
    result.error = std::move(error);
    return result;
}

Passcode ReadFromStdin() {
    const bool interactive = deskhubp::StdinIsTty();
    if (interactive) {
        std::fputs("Passcode: ", stderr);
        std::fflush(stderr);
    }

    std::string line;
    {
        const deskhubp::EchoOff quiet;
        if (!std::getline(std::cin, line)) {
            if (interactive) std::fputc('\n', stderr);
            return Fail("no passcode arrived on stdin");
        }
    }
    if (interactive) std::fputc('\n', stderr);
    return Accept(FirstLine(line));
}

Passcode ReadFromFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return Fail("cannot read the passcode file " + path);

    std::ostringstream contents;
    contents << file.rdbuf();
    return Accept(FirstLine(deskhub::ui::TrimAscii(contents.str())));
}

}

Passcode ResolvePasscode(const deskhub::cli::Command& command) {
    using deskhub::cli::PasscodeSource;

    switch (command.passcodeSource) {
        case PasscodeSource::Literal: return Accept(command.passcode);
        case PasscodeSource::Stdin: return ReadFromStdin();
        case PasscodeSource::File: return ReadFromFile(command.passcode);
        case PasscodeSource::Absent: break;
    }

    const char* fromEnv = std::getenv(kPasscodeEnvVar);
    if (fromEnv && *fromEnv) return Accept(FirstLine(fromEnv));

    Passcode none;
    none.ok = true;
    return none;
}

}
