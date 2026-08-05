#pragma once
#include <string>
#include <vector>

#include "deskhubp/net/UdpSocket.h"

#include "deskhub/protocol/Wire.h"

bool QuerySources(const NetAddr& server, std::vector<deskhub::SourceInfo>& out,
    const std::string& passcode = std::string());
