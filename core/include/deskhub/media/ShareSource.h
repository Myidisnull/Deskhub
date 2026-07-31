#pragma once
#include <cstdint>
#include <string>

namespace deskhub::media {

struct ShareSource {
    uint64_t targetId = 0;
    std::string name;
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

}
