#pragma once
#include <cstdint>
#include <string>

namespace deskhubp {
class TerminalHost;
}

bool RunTerminal(const std::string& addrUtf8, const std::string& passcode);
bool RunLocalTerminal(deskhubp::TerminalHost& host, uint32_t termId);
