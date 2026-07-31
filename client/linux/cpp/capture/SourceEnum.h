#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct ShareSource {
    uint32_t nodeId = 0;
    std::string name;
    int32_t x = 0, y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

std::vector<ShareSource> GetShareSources();

std::string ShareSourceError();

void ReleaseShareSources();
