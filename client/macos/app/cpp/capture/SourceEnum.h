#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct ShareSource {
    uint32_t displayId = 0;
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
};

std::vector<ShareSource> GetShareSources();
