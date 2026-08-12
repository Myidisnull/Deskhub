#include "deskhubp/system/MemoryFootprint.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <psapi.h>

namespace deskhubp {
namespace {

constexpr SIZE_T kBytesPerMb = SIZE_T{1024} * 1024;

}

int MemoryFootprintMb() {
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return -1;
    return int(pmc.PrivateUsage / kBytesPerMb);
}

}
