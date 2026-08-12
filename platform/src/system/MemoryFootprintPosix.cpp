#include "deskhubp/system/MemoryFootprint.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace deskhubp {

int MemoryFootprintMb() {
    std::FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return -1;

    char line[128];
    long kb = -1;
    while (std::fgets(line, sizeof(line), f)) {
        if (std::strncmp(line, "VmRSS:", 6) != 0) continue;
        kb = std::strtol(line + 6, nullptr, 10);
        break;
    }
    std::fclose(f);

    if (kb <= 0) return -1;
    return int(kb / 1024);
}

}
