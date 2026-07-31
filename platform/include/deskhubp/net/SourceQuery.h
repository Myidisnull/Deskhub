#pragma once
#include <vector>

#include "deskhubp/net/UdpSocket.h"

#include "deskhub/protocol/Wire.h"

bool QuerySources(const NetAddr& server, std::vector<deskhub::SourceInfo>& out);
