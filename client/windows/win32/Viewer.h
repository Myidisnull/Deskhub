#pragma once
#include <string>
#include <vector>

#include "deskhub/protocol/Wire.h"

bool RunViewer(const std::string& addrUtf8, const std::vector<deskhub::SourceInfo>& sources,
    bool control, const std::string& passcode = std::string(),
    const std::string& sessionKey = std::string());
