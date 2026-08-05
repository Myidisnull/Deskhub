#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct AdapterAddr {
    std::string name;
    std::string ip;
    uint8_t prefixLen = 24;
    bool virtualAdapter = false;
};

std::vector<AdapterAddr> ListLocalIPv4();
