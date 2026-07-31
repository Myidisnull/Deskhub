#pragma once
#include <string>
#include <vector>

struct AdapterAddr {
    std::wstring name;
    std::string ip;
};

std::vector<AdapterAddr> ListLocalIPv4();
