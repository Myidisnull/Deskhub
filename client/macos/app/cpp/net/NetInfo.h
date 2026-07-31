#pragma once
#include <string>
#include <vector>

struct AdapterAddr {
    std::string name;
    std::string ip;
};

std::vector<AdapterAddr> ListLocalIPv4();
